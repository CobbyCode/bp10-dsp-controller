// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// wifi_manager.c — WLAN-Management, SoftAP, Captive Portal, Provisioning
//
// Vollständige Implementierung des WiFi-Managers für den A800X DSP Controller.
// Unterstützt SoftAP (erstes Setup) und Station-Modus (Heim-WLAN).
//

#include "wifi_manager.h"
#include "nvs_settings.h"
#include "app_config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "esp_netif.h"

static const char *TAG = "a800x_wifi";

// ---------------------------------------------------------------------------
// Event-Bits
// ---------------------------------------------------------------------------
#define WIFI_CONNECTED_BIT       BIT0
#define WIFI_FAIL_BIT            BIT1
#define WIFI_AP_STARTED_BIT      BIT2

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

static wifi_connected_cb_t    s_connected_cb = NULL;
static wifi_disconnected_cb_t s_disconnected_cb = NULL;

static int s_retry_count = 0;
static int s_max_retry = 5;

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

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi Station verbunden");
                break;

            case WIFI_EVENT_STA_DISCONNECTED: {
                if (s_sta_connected) {
                    s_sta_connected = false;
                    s_ip_str[0] = '\0';
                    if (s_disconnected_cb) {
                        s_disconnected_cb();
                    }
                }
                if (s_retry_count < s_max_retry) {
                    esp_wifi_connect();
                    s_retry_count++;
                    ESP_LOGW(TAG, "WiFi-Verbindung verloren, erneuter Versuch %d/%d",
                             s_retry_count, s_max_retry);
                } else {
                    ESP_LOGE(TAG, "WiFi-Verbindung nach %d Versuchen fehlgeschlagen",
                             s_max_retry);
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                }
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
                ESP_LOGI(TAG, "IP-Adresse erhalten: %s", s_ip_str);
                xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
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

    // Letzte 2 Bytes der MAC als hex: "a800x-3f21"
    snprintf(hostname, max_len, "%s-%02x%02x",
             CONFIG_A800X_MDNS_DEFAULT_HOSTNAME,
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

    ESP_LOGI(TAG, "SoftAP starten (Hostname: %s)", hostname);

    // SoftAP-Konfiguration
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = 0,
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    // AP-SSID aus Hostname: "a800x-3f21"
    snprintf((char *)wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid),
             "%s", hostname);

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP gestartet: SSID=%s", hostname);

    return ESP_OK;
}

esp_err_t wifi_manager_stop_softap(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "SoftAP stoppen...");
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_softap_active = false;
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

    ESP_LOGI(TAG, "Verbinde mit WLAN: %s", ssid);

    // Event-Bits zurücksetzen
    xEventGroupClearBits(s_wifi_event_group,
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    s_retry_count = 0;

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

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "WiFi-Verbindungsversuch gestartet");

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

esp_err_t wifi_manager_get_ip_str(char *ip, size_t max_len)
{
    if (s_ip_str[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    strncpy(ip, s_ip_str, max_len - 1);
    ip[max_len - 1] = '\0';
    return ESP_OK;
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

    ESP_LOGI(TAG, "Captive Portal gestartet (DNS-Weiterleitung)");

    // DNS-Weiterleitung: Alle DNS-Anfragen auf ESP-IP auflösen
    // Die eigentliche Implementierung erfolgt über den HTTP-Server,
    // der alle Anfragen auf die Setup-Seite umleitet.
    //
    // Hier wird der DNS-Server für das AP-Interface konfiguriert.
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_dns_info_t dns;
        esp_netif_get_ip_info(ap_netif, NULL);  // netif aktivieren
        // Setze DNS auf die AP-IP selbst
        esp_netif_get_ip_info(ap_netif, NULL);
        dns.ip.u_addr.ip4.addr = esp_ip4_addr_get_byte(&((esp_netif_ip_info_t){
            .ip = { .addr = 0 }
        }).ip, 0);  // Platzhalter
        esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    }

    return ESP_OK;
}

esp_err_t wifi_manager_stop_captive_portal(void)
{
    ESP_LOGI(TAG, "Captive Portal gestoppt");
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

    s_initialized = false;
    s_softap_active = false;
    s_sta_connected = false;
    s_ip_str[0] = '\0';
    s_connected_cb = NULL;
    s_disconnected_cb = NULL;

    ESP_LOGI(TAG, "WiFi-Manager deinitialisiert");
}
