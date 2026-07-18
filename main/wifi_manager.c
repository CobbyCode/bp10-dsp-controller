// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// wifi_manager.c — WLAN-Management, SoftAP, Captive Portal, Provisioning
//
// Vollständige Implementierung des WiFi-Managers für den BP10 DSP Controller.
// Unterstützt SoftAP (erstes Setup) und Station-Modus (Heim-WLAN).
//
// Lifecycle:
//   BOOTING → AP_ONLY (keine Creds) oder AP_STA_CONNECTING (mit Creds)
//   AP_STA_CONNECTING → CONNECTED_AP_TRANSITION (STA+IP erhalten)
//   CONNECTED_AP_TRANSITION → CONNECTED_STA_ONLY (30 s AP-Shutdown-Timer)
//   CONNECTED_STA_ONLY → DISCONNECTED_RETRYING (Disconnect, 60 s Fallback)
//   DISCONNECTED_RETRYING → CONNECTED_AP_TRANSITION (reconnected) oder FALLBACK_AP (60 s abgelaufen)
//   FALLBACK_AP → CONNECTED_AP_TRANSITION (reconnected)
//

#include "wifi_manager.h"
#include "nvs_settings.h"
#include "app_config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "esp_netif.h"

static const char *TAG = "bp10_wifi";

// ---------------------------------------------------------------------------
// Event-Bits
// ---------------------------------------------------------------------------
#define WIFI_CONNECTED_BIT       BIT0
#define WIFI_FAIL_BIT            BIT1
#define WIFI_AP_STARTED_BIT      BIT2

// ---------------------------------------------------------------------------
// Timing-Konstanten
// ---------------------------------------------------------------------------
#define WIFI_USER_IDLE_TIMEOUT_S      1800   // Auto-Off bei Inaktivität
#define WIFI_FALLBACK_RETRY_WINDOW_S  60     // Neuverbindungsversuche bevor AP wieder an
#define WIFI_STA_RECONNECT_INTERVAL_S 5      // Pause zwischen Reconnect-Versuchen

// ---------------------------------------------------------------------------
// Interner Zustand
// ---------------------------------------------------------------------------
static EventGroupHandle_t s_wifi_event_group = NULL;
static bool s_initialized = false;
static bool s_softap_active = false;
static bool s_sta_connected = false;
static char s_ip_str[16] = {0};
static char s_mac_str[18] = {0};
static char s_hostname[32] = {0};
static char s_sta_ssid[33] = {0};
static wifi_lifecycle_state_t s_lifecycle_state = WIFI_LIFECYCLE_BOOTING;

static wifi_connected_cb_t    s_connected_cb = NULL;
static wifi_disconnected_cb_t s_disconnected_cb = NULL;

static int s_retry_count = 0;
static int s_max_retry = 5;
static bool s_auto_off_enabled = false;
static bool s_credentials_available = false;
static int64_t s_auto_off_deadline_us = 0;
static bool s_auto_off_task_started = false;
static SemaphoreHandle_t s_state_lock = NULL;
static wifi_manager_scan_state_t s_scan_state = WIFI_MANAGER_SCAN_IDLE;
static wifi_manager_scan_result_t s_scan_results[WIFI_MANAGER_SCAN_MAX_RESULTS];
static size_t s_scan_count = 0;
static esp_err_t s_scan_error = ESP_OK;
static char s_connection_state[16] = "idle";
static char s_connection_message[80] = "Not connected";

// Neue Timer für AP-Shutdown und Fallback
static int64_t s_fallback_deadline_us = 0;       // 0 = kein Timer aktiv
static int64_t s_last_reconnect_attempt_us = 0;
static bool s_ap_start_requested = false;        // AP-Start durch Fallback anfordern
static bool s_wifi_started = false;              // ob esp_wifi_start() bereits lief
static uint32_t s_recovery_timeout_s = WIFI_FALLBACK_RETRY_WINDOW_S;
static bool s_setup_captive_active = false;
static bool s_sta_connect_pending = false;
static int64_t s_sta_connect_deadline_us = 0;
static char s_pending_sta_ssid[33] = {0};
static char s_pending_sta_password[65] = {0};

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void start_fallback_timer(void);
static void cancel_fallback_timer(void);
static void lifecycle_set_state(wifi_lifecycle_state_t new_state);
static void do_stop_softap(void);
static void do_start_softap(void);
static void log_runtime_wifi_state(const char *context);

// ---------------------------------------------------------------------------
// Lifecycle Task (ersetzt den alten wifi_auto_off_task)
// ---------------------------------------------------------------------------
static void wifi_lifecycle_task(void *arg)
{
    while (s_initialized) {
        int64_t now = esp_timer_get_time();

        // Provisioning must not alter the AP/channel while the HTTP handler is
        // still returning its acknowledgement. The handler only schedules the
        // work; this task performs it after the response has left the socket.
        if (s_sta_connect_pending && now >= s_sta_connect_deadline_us) {
            char ssid[sizeof(s_pending_sta_ssid)];
            char password[sizeof(s_pending_sta_password)];
            if (s_state_lock) xSemaphoreTake(s_state_lock, portMAX_DELAY);
            snprintf(ssid, sizeof(ssid), "%s", s_pending_sta_ssid);
            snprintf(password, sizeof(password), "%s", s_pending_sta_password);
            memset(s_pending_sta_password, 0, sizeof(s_pending_sta_password));
            s_sta_connect_pending = false;
            s_sta_connect_deadline_us = 0;
            if (s_state_lock) xSemaphoreGive(s_state_lock);
            esp_err_t err = wifi_manager_connect_sta(ssid, password);
            memset(password, 0, sizeof(password));
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Deferred Wi-Fi connection could not start: %s",
                         esp_err_to_name(err));
                snprintf(s_connection_state, sizeof(s_connection_state), "failed");
                snprintf(s_connection_message, sizeof(s_connection_message),
                         "Unable to start Wi-Fi connection attempt");
            }
        }

        // --- Fallback-Timer (60 s ohne STA → AP wieder einschalten) ---
        if (s_fallback_deadline_us > 0 && now >= s_fallback_deadline_us) {
            ESP_LOGI(TAG, "Fallback-Timer abgelaufen — SoftAP wird wieder eingeschaltet");
            s_fallback_deadline_us = 0;
            if (!s_softap_active && !s_sta_connected) {
                s_ap_start_requested = true;
                lifecycle_set_state(WIFI_LIFECYCLE_FALLBACK_AP);
            }
        }

        // --- AP-Start-Anforderung ausführen (außerhalb des Locks) ---
        if (s_ap_start_requested) {
            s_ap_start_requested = false;
            do_start_softap();
        }

        // --- Auto-Off bei Inaktivität (altes Verhalten) ---
        if (s_auto_off_enabled && s_credentials_available &&
            s_auto_off_deadline_us > 0 && now >= s_auto_off_deadline_us) {
            ESP_LOGI(TAG, "WLAN Auto-Off: Inaktivitätszeit erreicht, WLAN wird abgeschaltet");
            esp_wifi_stop();
            s_softap_active = false;
            s_sta_connected = false;
            s_ip_str[0] = '\0';
            s_auto_off_deadline_us = 0;
        }

        // --- Reconnect-Logik im DISCONNECTED_RETRYING / FALLBACK_AP State ---
        if ((s_lifecycle_state == WIFI_LIFECYCLE_DISCONNECTED_RETRYING ||
             s_lifecycle_state == WIFI_LIFECYCLE_FALLBACK_AP) &&
            !s_sta_connected && s_credentials_available) {
            if (now - s_last_reconnect_attempt_us >=
                (int64_t)WIFI_STA_RECONNECT_INTERVAL_S * 1000000LL) {
                s_last_reconnect_attempt_us = now;
                ESP_LOGI(TAG, "Reconnect-Versuch im State %s",
                         wifi_manager_lifecycle_state_str());
                esp_wifi_connect();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    s_auto_off_task_started = false;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Lifecycle-Helfer
// ---------------------------------------------------------------------------

static void lifecycle_set_state(wifi_lifecycle_state_t new_state)
{
    const char *names[] = {
        "BOOTING", "AP_ONLY", "AP_STA_CONNECTING",
        "CONNECTED_AP_TRANSITION", "CONNECTED_STA_ONLY",
        "DISCONNECTED_RETRYING", "FALLBACK_AP"
    };
    ESP_LOGI(TAG, "Lifecycle: %s → %s",
             (s_lifecycle_state < sizeof(names)/sizeof(names[0]))
                ? names[s_lifecycle_state] : "?",
             (new_state < sizeof(names)/sizeof(names[0]))
                ? names[new_state] : "?");
    s_lifecycle_state = new_state;
}

static void start_fallback_timer(void)
{
    if (s_fallback_deadline_us > 0) {
        ESP_LOGD(TAG, "Fallback-Timer läuft bereits; Deadline bleibt unverändert");
        return;
    }
    s_fallback_deadline_us = esp_timer_get_time() +
                             (int64_t)s_recovery_timeout_s * 1000000LL;
    ESP_LOGI(TAG, "Fallback-Timer gestartet (%lu s)",
             (unsigned long)s_recovery_timeout_s);
}

static void cancel_fallback_timer(void)
{
    if (s_fallback_deadline_us > 0) {
        ESP_LOGI(TAG, "Fallback-Timer abgebrochen");
    }
    s_fallback_deadline_us = 0;
}

static void do_stop_softap(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    if (!s_softap_active && mode == WIFI_MODE_STA) return;

    ESP_LOGI(TAG, "Setup-SoftAP und Setup-Dienste werden gestoppt");
    wifi_manager_stop_captive_portal();
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_err_t dhcp_err = esp_netif_dhcps_stop(ap_netif);
        if (dhcp_err != ESP_OK && dhcp_err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            ESP_LOGW(TAG, "AP-DHCP-Server stoppen: %s", esp_err_to_name(dhcp_err));
        }
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    s_softap_active = false;
    ESP_LOGI(TAG, "SoftAP deaktiviert; WiFi läuft jetzt STA-only");
    log_runtime_wifi_state("after AP shutdown");
}

static void do_start_softap(void)
{
    if (s_softap_active) return;
    if (s_hostname[0] == '\0') {
        wifi_manager_generate_hostname(s_hostname, sizeof(s_hostname));
    }

    ESP_LOGI(TAG, "SoftAP wird gestartet (SSID: %s)", s_hostname);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = 0,
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = { .required = false },
        },
    };
    snprintf((char *)wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid),
             "%s", s_hostname);

    // Immer APSTA-Mode – STA-Interface wird für WLAN-Scan benötigt.
    // AP-Interface existiert, broadcastet aber nur wenn AP-Config gesetzt ist.
    if (s_wifi_started) {
        // WiFi läuft bereits → nur Mode wechseln und AP-Config setzen
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    } else {
        // Erststart: APSTA-Mode, nur AP-Config (STA nutzt Defaults)
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        s_wifi_started = true;
    }

    s_softap_active = true;
    ESP_LOGI(TAG, "SoftAP aktiv: SSID=%s", s_hostname);
    wifi_manager_start_captive_portal();
    log_runtime_wifi_state("after setup AP start");
}

static void log_runtime_wifi_state(const char *context)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    wifi_config_t ap_config = {0};
    esp_netif_dhcp_status_t dhcp_status = ESP_NETIF_DHCP_INIT;
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_err_t mode_err = esp_wifi_get_mode(&mode);
    esp_err_t ap_err = esp_wifi_get_config(WIFI_IF_AP, &ap_config);
    esp_err_t dhcp_err = ap_netif
        ? esp_netif_dhcps_get_status(ap_netif, &dhcp_status)
        : ESP_ERR_NOT_FOUND;
    const char *mode_name = mode == WIFI_MODE_STA ? "STA" :
                            mode == WIFI_MODE_AP ? "AP" :
                            mode == WIFI_MODE_APSTA ? "APSTA" : "NULL";
    ESP_LOGI(TAG,
             "Runtime WiFi [%s]: mode=%s(%s), STA=%s, IP=%s, "
             "AP-SSID=%s(%s), DHCP=%s(%s), setup-captive=%s, "
             "general-http=independent",
             context, mode_name, esp_err_to_name(mode_err),
             s_sta_connected ? "connected" : "disconnected",
             s_ip_str[0] ? s_ip_str : "none",
             ap_err == ESP_OK ? (const char *)ap_config.ap.ssid : "unavailable",
             esp_err_to_name(ap_err),
             dhcp_err == ESP_OK && dhcp_status == ESP_NETIF_DHCP_STARTED
                 ? "running" : "stopped",
             esp_err_to_name(dhcp_err),
             s_setup_captive_active ? "running" : "stopped");
}

// ---------------------------------------------------------------------------
// Event-Handler (WiFi & IP)
// ---------------------------------------------------------------------------
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi Station gestartet");
                break;

            case WIFI_EVENT_STA_CONNECTED: {
                wifi_event_sta_connected_t *event =
                    (wifi_event_sta_connected_t *)event_data;
                ESP_LOGI(TAG, "WiFi Station verbunden mit SSID");
                snprintf(s_connection_state, sizeof(s_connection_state), "connecting");
                snprintf(s_connection_message, sizeof(s_connection_message),
                         "Connected; waiting for IP address");
                // SSID merken
                if (event) {
                    size_t len = strnlen((const char *)event->ssid,
                                         sizeof(event->ssid));
                    len = len < sizeof(s_sta_ssid) - 1
                          ? len : sizeof(s_sta_ssid) - 1;
                    memcpy(s_sta_ssid, event->ssid, len);
                    s_sta_ssid[len] = '\0';
                }
                break;
            }

            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *event =
                    (wifi_event_sta_disconnected_t *)event_data;
                bool was_connected = s_sta_connected;
                if (was_connected) {
                    s_sta_connected = false;
                    s_ip_str[0] = '\0';
                    if (s_disconnected_cb) {
                        s_disconnected_cb();
                    }
                }

                if (!s_credentials_available) {
                    // Keine Creds → nichts zu reconnecten
                    ESP_LOGI(TAG, "Keine gespeicherten Creds — kein Reconnect");
                    if (s_lifecycle_state == WIFI_LIFECYCLE_AP_STA_CONNECTING) {
                        lifecycle_set_state(WIFI_LIFECYCLE_AP_ONLY);
                    }
                    break;
                }

                if (was_connected &&
                    s_lifecycle_state == WIFI_LIFECYCLE_CONNECTED_STA_ONLY) {
                    // War verbunden im STA-only-Modus → Fallback starten
                    ESP_LOGW(TAG, "STA-Verbindung im STA-only-Modus verloren — "
                             "Fallback-Timer starten");
                    lifecycle_set_state(WIFI_LIFECYCLE_DISCONNECTED_RETRYING);
                    start_fallback_timer();
                    s_retry_count = 0;
                    // Ersten Reconnect sofort versuchen
                    esp_wifi_connect();
                    s_last_reconnect_attempt_us = esp_timer_get_time();
                    break;
                }

                // Standard-Retry (AP+STA-Modus oder schon im Fallback)
                if (s_retry_count < s_max_retry) {
                    esp_wifi_connect();
                    s_retry_count++;
                    ESP_LOGW(TAG, "WiFi-Verbindung verloren, erneuter Versuch %d/%d",
                             s_retry_count, s_max_retry);
                } else {
                    ESP_LOGE(TAG, "WiFi-Verbindung nach %d Versuchen fehlgeschlagen",
                             s_max_retry);
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    snprintf(s_connection_state, sizeof(s_connection_state), "failed");
                    const char *safe_reason =
                        "Connection failed; check SSID, password, and signal";
                    if (event && event->reason == WIFI_REASON_NO_AP_FOUND) {
                        safe_reason = "Network not found or signal is too weak";
                    } else if (event &&
                               (event->reason == WIFI_REASON_AUTH_FAIL ||
                                event->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT)) {
                        safe_reason = "Authentication failed; check the Wi-Fi password";
                    }
                    snprintf(s_connection_message, sizeof(s_connection_message),
                             "%s", safe_reason);

                    // Wenn alle Retries erschöpft: Fallback starten falls nötig
                    if (!s_softap_active && !s_sta_connected &&
                        s_lifecycle_state != WIFI_LIFECYCLE_FALLBACK_AP) {
                        start_fallback_timer();
                        lifecycle_set_state(WIFI_LIFECYCLE_DISCONNECTED_RETRYING);
                    }
                }
                break;
            }

            case WIFI_EVENT_SCAN_DONE: {
                wifi_event_sta_scan_done_t *event =
                    (wifi_event_sta_scan_done_t *)event_data;
                uint16_t found = 0;
                esp_err_t err = event && event->status != 0
                    ? ESP_FAIL : esp_wifi_scan_get_ap_num(&found);
                wifi_ap_record_t *records = NULL;
                if (err == ESP_OK && found > 0) {
                    records = calloc(found, sizeof(*records));
                    if (!records) err = ESP_ERR_NO_MEM;
                }
                if (err == ESP_OK && found > 0) {
                    err = esp_wifi_scan_get_ap_records(&found, records);
                }

                if (s_state_lock) xSemaphoreTake(s_state_lock, portMAX_DELAY);
                s_scan_count = 0;
                s_scan_error = err;
                if (err == ESP_OK) {
                    for (uint16_t i = 0; i < found; ++i) {
                        const char *ssid = (const char *)records[i].ssid;
                        if (!ssid || ssid[0] == '\0') continue;
                        size_t existing = s_scan_count;
                        for (size_t j = 0; j < s_scan_count; ++j) {
                            if (strcmp(s_scan_results[j].ssid, ssid) == 0) {
                                existing = j;
                                break;
                            }
                        }
                        wifi_manager_scan_result_t candidate = {0};
                        snprintf(candidate.ssid, sizeof(candidate.ssid), "%s", ssid);
                        candidate.rssi = records[i].rssi;
                        candidate.secure = records[i].authmode != WIFI_AUTH_OPEN;
                        if (existing < s_scan_count) {
                            if (candidate.rssi > s_scan_results[existing].rssi) {
                                s_scan_results[existing] = candidate;
                            }
                        } else if (s_scan_count < WIFI_MANAGER_SCAN_MAX_RESULTS) {
                            s_scan_results[s_scan_count++] = candidate;
                        } else if (candidate.rssi < s_scan_results[s_scan_count - 1].rssi) {
                            continue;
                        } else {
                            s_scan_results[s_scan_count - 1] = candidate;
                        }
                        for (size_t a = 0; a < s_scan_count; ++a) {
                            for (size_t b = a + 1; b < s_scan_count; ++b) {
                                if (s_scan_results[b].rssi > s_scan_results[a].rssi) {
                                    wifi_manager_scan_result_t tmp = s_scan_results[a];
                                    s_scan_results[a] = s_scan_results[b];
                                    s_scan_results[b] = tmp;
                                }
                            }
                        }
                    }
                    s_scan_state = WIFI_MANAGER_SCAN_DONE;
                } else {
                    s_scan_state = WIFI_MANAGER_SCAN_FAILED;
                }
                if (s_state_lock) xSemaphoreGive(s_state_lock);
                free(records);
                ESP_LOGI(TAG, "Wi-Fi scan completed (%u visible unique results)",
                         (unsigned)s_scan_count);
                break;
            }

            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "SoftAP gestartet");
                s_softap_active = true;
                xEventGroupSetBits(s_wifi_event_group, WIFI_AP_STARTED_BIT);
                break;

            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "SoftAP gestoppt");
                s_softap_active = false;
                break;

            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *event =
                    (wifi_event_ap_staconnected_t *)event_data;
                ESP_LOGI(TAG, "Client mit SoftAP verbunden (MAC: " MACSTR ")",
                         MAC2STR(event->mac));
                break;
            }

            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *event =
                    (wifi_event_ap_stadisconnected_t *)event_data;
                ESP_LOGI(TAG, "Client von SoftAP getrennt (MAC: " MACSTR ")",
                         MAC2STR(event->mac));
                break;
            }

            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
                esp_ip4addr_ntoa(&event->ip_info.ip, s_ip_str, sizeof(s_ip_str));
                s_sta_connected = true;
                s_retry_count = 0;
                cancel_fallback_timer();
                ESP_LOGI(TAG, "IP-Adresse erhalten: %s", s_ip_str);
                snprintf(s_connection_state, sizeof(s_connection_state), "connected");
                snprintf(s_connection_message, sizeof(s_connection_message),
                         "Mit Heimnetz verbunden");
                xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

                // A valid home-network IP ends setup immediately. The general
                // HTTP server is interface-independent and remains available.
                if (s_softap_active) {
                    lifecycle_set_state(WIFI_LIFECYCLE_CONNECTED_AP_TRANSITION);
                    do_stop_softap();
                    lifecycle_set_state(WIFI_LIFECYCLE_CONNECTED_STA_ONLY);
                } else {
                    lifecycle_set_state(WIFI_LIFECYCLE_CONNECTED_STA_ONLY);
                }
                log_runtime_wifi_state("STA got IP");

                if (s_connected_cb) {
                    s_connected_cb();
                }
                break;
            }

            case IP_EVENT_STA_LOST_IP:
                ESP_LOGW(TAG, "IP-Adresse verloren");
                s_ip_str[0] = '\0';
                break;

            default:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// Initialisierung
// ---------------------------------------------------------------------------

esp_err_t wifi_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "WiFi-Manager initialisieren...");

    // Event-Group erstellen
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        return ESP_ERR_NO_MEM;
    }
    s_state_lock = xSemaphoreCreateMutex();
    if (!s_state_lock) return ESP_ERR_NO_MEM;

    // Netzwerk-Interface initialisieren
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Standard-Interfaces erstellen (AP + Station)
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    // WiFi-Konfiguration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Event-Handler registrieren
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_LOST_IP, &wifi_event_handler, NULL, NULL));

    s_initialized = true;
    lifecycle_set_state(WIFI_LIFECYCLE_BOOTING);

    // Lifecycle-Task starten (ersetzt wifi_auto_off_task)
    if (!s_auto_off_task_started) {
        s_auto_off_task_started = xTaskCreate(wifi_lifecycle_task,
                                               "wifi_lifecycle",
                                               4096, NULL, 3, NULL) == pdPASS;
    }
    ESP_LOGI(TAG, "WiFi-Manager initialisiert");

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Hostname / MAC
// ---------------------------------------------------------------------------

void wifi_manager_generate_hostname(char *hostname, size_t max_len)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    // Letzte 2 Bytes der MAC als hex: "bp10-xxxx"
    snprintf(hostname, max_len, "%s-%02x%02x",
             CONFIG_BP10_MDNS_DEFAULT_HOSTNAME,
             mac[4], mac[5]);

    // Für spätere Referenz speichern
    strncpy(s_hostname, hostname, sizeof(s_hostname) - 1);
    s_hostname[sizeof(s_hostname) - 1] = '\0';

    // MAC-String speichern
    snprintf(s_mac_str, sizeof(s_mac_str), MACSTR, MAC2STR(mac));
}

void wifi_manager_get_mac_str(char *mac)
{
    if (s_mac_str[0] == '\0') {
        uint8_t mac_buf[6];
        esp_efuse_mac_get_default(mac_buf);
        snprintf(s_mac_str, sizeof(s_mac_str), MACSTR, MAC2STR(mac_buf));
    }
    strncpy(mac, s_mac_str, 18);
    mac[17] = '\0';
}

// ---------------------------------------------------------------------------
// SoftAP
// ---------------------------------------------------------------------------

esp_err_t wifi_manager_start_softap(const char *hostname)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "WiFi-Manager nicht initialisiert");
        return ESP_ERR_INVALID_STATE;
    }

    if (hostname && hostname[0] != '\0') {
        strncpy(s_hostname, hostname, sizeof(s_hostname) - 1);
        s_hostname[sizeof(s_hostname) - 1] = '\0';
    }

    ESP_LOGI(TAG, "SoftAP starten (Hostname: %s)", s_hostname);

    do_start_softap();

    if (!s_sta_connected && !s_credentials_available) {
        lifecycle_set_state(WIFI_LIFECYCLE_AP_ONLY);
    } else if (!s_sta_connected) {
        lifecycle_set_state(WIFI_LIFECYCLE_AP_STA_CONNECTING);
    }

    return ESP_OK;
}

esp_err_t wifi_manager_stop_softap(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    do_stop_softap();
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Station
// ---------------------------------------------------------------------------

esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "WiFi-Manager nicht initialisiert");
        return ESP_ERR_INVALID_STATE;
    }

    if (ssid == NULL || ssid[0] == '\0') {
        ESP_LOGE(TAG, "Keine SSID angegeben");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting Wi-Fi station connection attempt");
    snprintf(s_connection_state, sizeof(s_connection_state), "connecting");
    snprintf(s_connection_message, sizeof(s_connection_message), "Connecting…");

    // Event-Bits zurücksetzen
    xEventGroupClearBits(s_wifi_event_group,
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    s_retry_count = 0;
    cancel_fallback_timer();

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password && password[0] != '\0') {
        strncpy((char *)wifi_config.sta.password, password,
                sizeof(wifi_config.sta.password) - 1);
    }

    // Thresholds setzen
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    // Keep setup AP while provisioning; boot with stored credentials is STA-only.
    esp_wifi_set_mode(s_softap_active ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    if (!s_wifi_started) {
        ESP_ERROR_CHECK(esp_wifi_start());
        s_wifi_started = true;
    }
    ESP_ERROR_CHECK(esp_wifi_connect());

    // Lifecycle-Update
    if (s_softap_active && s_credentials_available) {
        lifecycle_set_state(WIFI_LIFECYCLE_AP_STA_CONNECTING);
    } else if (s_softap_active) {
        lifecycle_set_state(WIFI_LIFECYCLE_AP_STA_CONNECTING);
    }

    ESP_LOGI(TAG, "WiFi-Verbindungsversuch gestartet");

    return ESP_OK;
}

esp_err_t wifi_manager_schedule_sta_connect(const char *ssid,
                                            const char *password,
                                            uint32_t delay_ms)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!ssid || ssid[0] == '\0') return ESP_ERR_INVALID_ARG;

    if (s_state_lock) xSemaphoreTake(s_state_lock, portMAX_DELAY);
    snprintf(s_pending_sta_ssid, sizeof(s_pending_sta_ssid), "%s", ssid);
    snprintf(s_pending_sta_password, sizeof(s_pending_sta_password), "%s",
             password ? password : "");
    s_sta_connect_deadline_us = esp_timer_get_time() +
                                (int64_t)delay_ms * 1000LL;
    s_sta_connect_pending = true;
    if (s_state_lock) xSemaphoreGive(s_state_lock);

    snprintf(s_connection_state, sizeof(s_connection_state), "connecting");
    snprintf(s_connection_message, sizeof(s_connection_message),
             "Saved; connection starts shortly");
    ESP_LOGI(TAG, "Wi-Fi station connection scheduled in %lu ms",
             (unsigned long)delay_ms);
    return ESP_OK;
}

esp_err_t wifi_manager_disconnect_sta(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    esp_wifi_disconnect();
    s_sta_connected = false;
    s_ip_str[0] = '\0';
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

bool wifi_manager_is_connected(void)
{
    return s_sta_connected;
}

bool wifi_manager_is_softap_active(void)
{
    return s_softap_active;
}

esp_err_t wifi_manager_get_ip_str(char *ip, size_t max_len)
{
    if (s_ip_str[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    strncpy(ip, s_ip_str, max_len - 1);
    ip[max_len - 1] = '\0';
    return ESP_OK;
}

esp_err_t wifi_manager_get_sta_ssid(char *ssid, size_t max_len)
{
    if (!s_sta_connected || s_sta_ssid[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    strncpy(ssid, s_sta_ssid, max_len - 1);
    ssid[max_len - 1] = '\0';
    return ESP_OK;
}

esp_err_t wifi_manager_get_hostname(char *hostname, size_t max_len)
{
    if (s_hostname[0] == '\0') {
        wifi_manager_generate_hostname(s_hostname, sizeof(s_hostname));
    }
    strncpy(hostname, s_hostname, max_len - 1);
    hostname[max_len - 1] = '\0';
    return ESP_OK;
}

int wifi_manager_get_ap_shutdown_remaining_sec(void)
{
    return 0; // setup AP stops synchronously on IP_EVENT_STA_GOT_IP
}

wifi_lifecycle_state_t wifi_manager_get_lifecycle_state(void)
{
    return s_lifecycle_state;
}

const char *wifi_manager_lifecycle_state_str(void)
{
    switch (s_lifecycle_state) {
        case WIFI_LIFECYCLE_BOOTING:              return "booting";
        case WIFI_LIFECYCLE_AP_ONLY:              return "ap_only";
        case WIFI_LIFECYCLE_AP_STA_CONNECTING:    return "ap_sta_connecting";
        case WIFI_LIFECYCLE_CONNECTED_AP_TRANSITION: return "connected_ap_transition";
        case WIFI_LIFECYCLE_CONNECTED_STA_ONLY:   return "connected_sta_only";
        case WIFI_LIFECYCLE_DISCONNECTED_RETRYING:  return "disconnected_retrying";
        case WIFI_LIFECYCLE_FALLBACK_AP:          return "fallback_ap";
        default:                                   return "unknown";
    }
}

bool wifi_manager_has_credentials(void)
{
    return s_credentials_available;
}

void wifi_manager_set_has_credentials(bool has)
{
    s_credentials_available = has;
}

esp_err_t wifi_manager_start_scan(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_state_lock) xSemaphoreTake(s_state_lock, portMAX_DELAY);
    if (s_scan_state == WIFI_MANAGER_SCAN_RUNNING) {
        if (s_state_lock) xSemaphoreGive(s_state_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_scan_state = WIFI_MANAGER_SCAN_RUNNING;
    s_scan_count = 0;
    s_scan_error = ESP_OK;
    if (s_state_lock) xSemaphoreGive(s_state_lock);

    wifi_scan_config_t config = {
        .ssid = NULL, .bssid = NULL, .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t err = esp_wifi_scan_start(&config, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_scan_start failed: %s (0x%x)",
                 esp_err_to_name(err), err);
        if (s_state_lock) xSemaphoreTake(s_state_lock, portMAX_DELAY);
        s_scan_state = WIFI_MANAGER_SCAN_FAILED;
        s_scan_error = err;
        if (s_state_lock) xSemaphoreGive(s_state_lock);
    }
    return err;
}

wifi_manager_scan_state_t wifi_manager_get_scan_results(
    wifi_manager_scan_result_t *results, size_t capacity, size_t *count,
    esp_err_t *scan_error)
{
    if (s_state_lock) xSemaphoreTake(s_state_lock, portMAX_DELAY);
    wifi_manager_scan_state_t state = s_scan_state;
    size_t n = s_scan_count < capacity ? s_scan_count : capacity;
    if (results && n) memcpy(results, s_scan_results, n * sizeof(*results));
    if (count) *count = n;
    if (scan_error) *scan_error = s_scan_error;
    if (s_state_lock) xSemaphoreGive(s_state_lock);
    return state;
}

void wifi_manager_get_connection_status(char *state, size_t state_len,
                                        char *message, size_t message_len)
{
    snprintf(state, state_len, "%s", s_connection_state);
    snprintf(message, message_len, "%s", s_connection_message);
}

void wifi_manager_configure_auto_off(bool enabled, uint32_t initial_timeout_s,
                                     bool credentials_available)
{
    s_auto_off_enabled = enabled;
    s_credentials_available = credentials_available;
    s_recovery_timeout_s = initial_timeout_s > 0
        ? initial_timeout_s : WIFI_FALLBACK_RETRY_WINDOW_S;
    if (enabled && credentials_available && initial_timeout_s > 0) {
        s_auto_off_deadline_us = esp_timer_get_time() +
                                 (int64_t)initial_timeout_s * 1000000LL;
    } else {
        s_auto_off_deadline_us = 0;
    }
    ESP_LOGI(TAG, "WLAN Auto-Off: %s", enabled ? "aktiv" : "deaktiviert (dauerhaft erreichbar)");
}

void wifi_manager_note_user_activity(void)
{
    if (s_auto_off_enabled && s_credentials_available) {
        s_auto_off_deadline_us = esp_timer_get_time() +
                                 (int64_t)WIFI_USER_IDLE_TIMEOUT_S * 1000000LL;
    }
}

bool wifi_manager_auto_off_enabled(void)
{
    return s_auto_off_enabled;
}

// ---------------------------------------------------------------------------
// Captive Portal
// ---------------------------------------------------------------------------

esp_err_t wifi_manager_start_captive_portal(void)
{
    if (!s_softap_active) {
        ESP_LOGE(TAG, "SoftAP nicht aktiv — Captive Portal nicht möglich");
        return ESP_ERR_INVALID_STATE;
    }

    s_setup_captive_active = true;
    ESP_LOGI(TAG, "Setup-Captive-Dienst gestartet (allgemeiner HTTP-Server bleibt separat)");
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_dns_info_t dns;
        esp_netif_get_ip_info(ap_netif, NULL);
        dns.ip.u_addr.ip4.addr = esp_ip4_addr_get_byte(
            &((esp_netif_ip_info_t){ .ip = { .addr = 0 } }).ip, 0);
        esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    }
    return ESP_OK;
}

esp_err_t wifi_manager_stop_captive_portal(void)
{
    if (s_setup_captive_active) ESP_LOGI(TAG, "Setup-Captive-Dienst gestoppt");
    s_setup_captive_active = false;
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void wifi_manager_on_connected(wifi_connected_cb_t cb)
{
    s_connected_cb = cb;
}

void wifi_manager_on_disconnected(wifi_disconnected_cb_t cb)
{
    s_disconnected_cb = cb;
}

// ---------------------------------------------------------------------------
// Deinitialisierung
// ---------------------------------------------------------------------------

void wifi_manager_deinit(void)
{
    if (!s_initialized) return;

    ESP_LOGI(TAG, "WiFi-Manager deinitialisieren...");

    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();

    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    if (s_state_lock) {
        vSemaphoreDelete(s_state_lock);
        s_state_lock = NULL;
    }

    s_initialized = false;
    s_softap_active = false;
    s_sta_connected = false;
    s_ip_str[0] = '\0';
    s_sta_ssid[0] = '\0';
    s_fallback_deadline_us = 0;
    s_wifi_started = false;
    s_connected_cb = NULL;
    s_disconnected_cb = NULL;

    ESP_LOGI(TAG, "WiFi-Manager deinitialisiert");
}
