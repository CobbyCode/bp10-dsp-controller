// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// main.c — AIYIMA BP10 DSP Controller — Einstiegspunkt
//
// Initialisierungsreihenfolge:
// 1. NVS
// 2. USB-Host starten (nicht blockierend)
// 3. Netzwerk: WiFi + mDNS + HTTP-Server
// 4. DSP-Zustand beim Connect:
//    - Gespeicherte DSP-Konfiguration aus NVS laden & anwenden
//    - Jedes Modul per Readback bestätigen
//    - Ohne gespeicherte Konfiguration: nur aktuellen Zustand lesen
// 5. Normalbetrieb: Web-UI, OTA, Config-IO
//
// Wichtig: Das System startet auch ohne angeschlossenen BP10.
// Die Web-UI zeigt dann "DSP nicht verfügbar".
//
// Niemals DSP-Flash-Save 0xFD verwenden!
//

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "app_config.h"

#include "usb_host_ctrl.h"
#include "dsp_model.h"
#include "mvs_device_runtime.h"
#include "nvs_settings.h"
#include "wifi_manager.h"
#include "mdns_service.h"
#include "http_server.h"
#include "api_handlers.h"
#include "ota_update.h"
#include "config_io.h"

static const char *TAG = "bp10_main";

// Globaler DSP-Status (für API/UI)
bool g_dsp_connected = false;
bool g_dsp_ns_state = false;  // Noise Suppressor gelesener Zustand

// Forward declarations
static void init_nvs(void);
static void init_usb_host(void);
static void init_network(void);
static void init_http_server(void);
static void dsp_boot_apply_task(void *arg);
static void dsp_boot_readonly_task(void *arg);
#if CONFIG_BP10_DIAGNOSTIC_NS_BOOT_TEST
static void dsp_test_task(void *arg);
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "=== AIYIMA BP10 DSP Controller ===");
    ESP_LOGI(TAG, "Firmware version: " APP_VERSION);

    // 1. NVS initialisieren
    init_nvs();
    ESP_ERROR_CHECK(dsp_model_init());
    mvs_device_runtime_clear();

    // 2. OTA initialisieren (Partition-Check, Rollback-Status)
    ota_init();

    // 3. Rollback-Selbsttest (falls PENDING_VERIFY)
    ota_perform_self_test();

    // 4. USB-Host starten (nicht blockierend, timeout 5 s)
    init_usb_host();

    // 5. Netzwerk starten: WiFi + mDNS + HTTP
    //    Läuft immer, auch ohne DSP
    init_network();
    init_http_server();

    ESP_LOGI(TAG, "Initialisierung abgeschlossen.");
    ESP_LOGI(TAG, "Web-UI: http://%s.local", CONFIG_BP10_MDNS_DEFAULT_HOSTNAME);

    // Normalbetrieb: Status-Loop
    while (1) {
        bool was_connected = g_dsp_connected;
        bool transport_connected = usb_host_ctrl_is_device_connected();

        if (transport_connected && !mvs_device_runtime_is_ready()) {
            esp_err_t identify_err = mvs_device_runtime_identify();
            if (identify_err != ESP_OK) {
                ESP_LOGW(TAG, "ACP device identification incomplete: %s",
                         esp_err_to_name(identify_err));
            }
        } else if (!transport_connected && mvs_device_runtime_is_ready()) {
            mvs_device_runtime_clear();
        }
        g_dsp_connected = transport_connected && mvs_device_runtime_is_ready();

        // Bei späterer Verbindung (Hot-Plug/Reconnect):
        // gleicher Ablauf wie beim Boot – Konfiguration anwenden oder nur lesen.
        if (g_dsp_connected && !was_connected) {
            ESP_LOGI(TAG, "DSP profile ready – initializing device state...");
            dsp_boot_apply_task(NULL);
        }

        // Bei Trennung: Status vermerken
        if (!g_dsp_connected && was_connected) {
            ESP_LOGI(TAG, "BP10 getrennt – warte auf Wiederverbindung.");
            g_dsp_ns_state = false;
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------
static void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS-Partition neu erstellen...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialisiert");
}

// ---------------------------------------------------------------------------
// USB-Host (optional – System startet auch ohne DSP)
// ---------------------------------------------------------------------------
static void init_usb_host(void)
{
    ESP_LOGI(TAG, "USB-Host initialisieren (dauerhaft, kein Timeout)...");

    // VBUS einschalten (falls konfiguriert)
    usb_host_vbus_enable(true);

    // USB-Host initialisieren (nicht blockierend)
    esp_err_t err = usb_host_ctrl_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB-Host-Init fehlgeschlagen: %s", esp_err_to_name(err));
        g_dsp_connected = false;
        return;
    }

    // Nicht blockierend: Client-Task läuft dauerhaft im Hintergrund
    // und meldet Devices via Callback. Haupt-Loop erkennt später
    // verbundene Geräte asynchron.
    ESP_LOGI(TAG, "USB-Host läuft dauerhaft (Client-Task aktiv).");

    g_dsp_connected = false;
    ESP_LOGI(TAG, "USB transport waits for a supported device profile.");
}

// ---------------------------------------------------------------------------
// Boot / Hot-Plug: Gespeicherte DSP-Konfiguration anwenden oder nur lesen.
//
// Ablauf:
//   1. NVS-Konfiguration laden
//   2. Falls vorhanden: vollständig auf DSP anwenden
//   3. Jedes Modul per Readback bestätigen
//   4. Nur bestätigte Werte als aktiv übernehmen
//   5. Bei Teilfehlern pro Modul melden, Rest bleibt gültig
//   6. Falls keine Config: DSP nur lesen, NICHT automatisch speichern
//
// Kein DSP-Flash-Save 0xFD!
// ---------------------------------------------------------------------------

static void confirm_and_log_module(const char *name, bool ok)
{
    if (ok) {
        ESP_LOGI(TAG, "  ✅ %s bestätigt", name);
    } else {
        ESP_LOGW(TAG, "  ⚠️ %s Readback-Fehler – Modul wurde möglicherweise "
                       "nicht übernommen", name);
    }
}

/**
 * @brief DSP-Konfiguration anwenden oder nur lesen (Boot/Reconnect).
 *
 * Wird sowohl beim initialen Boot als auch bei Hot-Plug/Reconnect aufgerufen.
 * Läuft inline (kein separater Task), da der Aufrufer bereits im Main-Loop-
 * Kontext blockieren kann.
 */
static void dsp_boot_apply_task(void *arg)
{
    (void)arg;

    if (!g_dsp_connected) {
        ESP_LOGW(TAG, "dsp_boot_apply: DSP nicht verbunden – Abbruch");
        return;
    }

    // Kurze Wartezeit für USB-Stabilität nach Hot-Plug
    vTaskDelay(pdMS_TO_TICKS(500));

    const mvs_device_profile_t *device = dsp_model_get_device_profile();
    if (!device->valid) return;
    if (device->kind == MVS_DEVICE_GENERIC_ACP) {
        ESP_LOGI(TAG, "Generic ACP: read-only reconnect initialization; A800X NVS ignored");
        dsp_boot_readonly_task(NULL);
        return;
    }

    // Prüfen ob eine gespeicherte Konfiguration existiert
    if (!nvs_settings_has_dsp_config()) {
        ESP_LOGI(TAG, "Keine gespeicherte DSP-Konfiguration – "
                       "lese aktuellen Zustand (nur Readback, kein Save)");
        dsp_boot_readonly_task(NULL);
        return;
    }

    // --- Gespeicherte Konfiguration laden ---
    dsp_profile_t saved;
    esp_err_t err = nvs_settings_load_dsp_config(&saved);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS-Konfiguration laden fehlgeschlagen: %s – "
                       "falle zurück auf Readback", esp_err_to_name(err));
        dsp_boot_readonly_task(NULL);
        return;
    }

    ESP_LOGI(TAG, "Gespeicherte DSP-Konfiguration geladen – wende an...");

    // --- Vollständig auf DSP anwenden ---
    err = dsp_model_apply_profile(&saved);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Profil anwenden fehlgeschlagen: %s", esp_err_to_name(err));
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(200));

    // --- Jedes Modul per Readback bestätigen ---
    ESP_LOGI(TAG, "Readback-Bestätigung aller Module:");
    int ok_count = 0;
    int fail_count = 0;

    // Noise Suppressor
    dsp_profile_t readback;
    err = dsp_model_readback(&readback);
    if (err == ESP_OK) {
        bool ns_ok = (readback.noise_suppressor_enabled == saved.noise_suppressor_enabled);
        confirm_and_log_module("Noise Suppressor", ns_ok);
        if (ns_ok) ok_count++; else fail_count++;
        g_dsp_ns_state = readback.noise_suppressor_enabled;
    } else {
        ESP_LOGW(TAG, "  ⚠️ Vollständiger Readback fehlgeschlagen – "
                       "Einzel-Modul-Checks folgen");
    }

    // Virtual Bass (separater Readback falls Voll-Readback fehlschlug)
    {
        dsp_profile_t vb_check;
        err = dsp_model_readback(&vb_check);
        if (err == ESP_OK) {
            bool vb_ok = (vb_check.virtual_bass_enabled == saved.virtual_bass_enabled);
            confirm_and_log_module("Virtual Bass", vb_ok);
            if (vb_ok) ok_count++; else fail_count++;
        } else {
            confirm_and_log_module("Virtual Bass", false);
            fail_count++;
        }
    }

    // Silence Detector
    {
        bool sd_enabled = false;
        uint16_t sd_amplitude = 0;
        err = dsp_model_read_silence_detector(&sd_enabled, &sd_amplitude);
        bool sd_ok = (err == ESP_OK) && (sd_enabled == saved.silence_detector_enabled);
        confirm_and_log_module("Silence Detector", sd_ok);
        if (sd_ok) ok_count++; else fail_count++;
    }

    // PreEQ
    {
        mvs_preeq_state_t peq_state;
        err = dsp_model_read_preeq(&peq_state);
        bool peq_ok = (err == ESP_OK) &&
                      (memcmp(&peq_state, &saved.preeq, sizeof(peq_state)) == 0);
        confirm_and_log_module("PreEQ", peq_ok);
        if (peq_ok) ok_count++; else fail_count++;
    }

    // DRC
    {
        mvs_drc_packed_state_t drc_state;
        err = dsp_model_read_drc(&drc_state);
        bool drc_ok = (err == ESP_OK) &&
                      (memcmp(&drc_state, &saved.drc, sizeof(drc_state)) == 0);
        confirm_and_log_module("DRC", drc_ok);
        if (drc_ok) ok_count++; else fail_count++;
    }

    ESP_LOGI(TAG, "Reconnect-Konfiguration bestätigt: %d/%d Module%s",
             ok_count, ok_count + fail_count,
             fail_count > 0 ? " (Teilfehler – nicht bestätigte Werte werden "
                              "nicht erneut geschrieben)" : "");

#if CONFIG_BP10_DIAGNOSTIC_NS_BOOT_TEST
    ESP_LOGW(TAG, "Diagnosemodus: Noise-Suppressor-Boot-Test wird gestartet");
    xTaskCreatePinnedToCore(dsp_test_task, "dsp_test", 4096, NULL, 3, NULL, 1);
#endif
}

/**
 * @brief DSP nur lesen, nichts verändern, nicht speichern.
 *
 * Wird verwendet wenn keine gespeicherte Konfiguration existiert.
 */
static void dsp_boot_readonly_task(void *arg)
{
    (void)arg;

    if (!g_dsp_connected) {
        ESP_LOGW(TAG, "dsp_boot_readonly: DSP nicht verbunden – Abbruch");
        return;
    }

    dsp_profile_t profile;
    esp_err_t err = dsp_model_readback(&profile);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Readback (readonly) fehlgeschlagen: %s", esp_err_to_name(err));
        return;
    }

    g_dsp_ns_state = profile.noise_suppressor_enabled;
    ESP_LOGI(TAG, "DSP-Zustand gelesen (kein Auto-Save): Noise Suppressor=%s, "
                   "Virtual Bass=%s, PreEQ=%s, DRC=%s",
             profile.noise_suppressor_enabled ? "EIN" : "AUS",
             profile.virtual_bass_enabled ? "EIN" : "AUS",
             profile.preeq.block_enabled ? "EIN" : "AUS",
             profile.drc.enabled ? "EIN" : "AUS");
}

// ---------------------------------------------------------------------------
// Noise Suppressor Test-Task
//
// Führt einen Read/Write/Read-Zyklus für Noise Suppressor (0x88) durch:
// 1. Aktuellen Zustand lesen
// 2. Noise Suppressor AUS-schalten
// 3. Erneut lesen → bestätigen
// 4. Noise Suppressor EIN-schalten
// 5. Erneut lesen → bestätigen
// 6. Globalen Status aktualisieren
// ---------------------------------------------------------------------------
#if CONFIG_BP10_DIAGNOSTIC_NS_BOOT_TEST
static void dsp_test_task(void *arg)
{
    ESP_LOGI(TAG, "DSP-Test-Task gestartet");
    vTaskDelay(pdMS_TO_TICKS(2000));  // Warten auf USB-Stabilität

    uint8_t frame[16];
    uint8_t report[256];
    uint16_t report_len;
    esp_err_t err;
    uint8_t ns_effect_id = dsp_model_get_effect_id_ns();
    if (ns_effect_id == 0) goto test_end;

    // ---------- 1. Noise Suppressor lesen ----------
    ESP_LOGI(TAG, "=== Noise Suppressor Test ===");
    ESP_LOGI(TAG, "Schritt 1: Noise Suppressor lesen...");

    mvs_build_query_frame(ns_effect_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NS-Query senden fehlgeschlagen: %s", esp_err_to_name(err));
        goto test_end;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK || report_len < 14) {
        ESP_LOGE(TAG, "NS-Readback fehlgeschlagen: %s (len=%d)",
                 esp_err_to_name(err), report_len);
        goto test_end;
    }

    bool ns_enabled;
    int16_t ns_threshold;
    uint16_t ns_ratio, ns_attack, ns_release;
    err = mvs_decode_noise_suppressor(report + 5, report_len - 5,
                                       &ns_enabled, &ns_threshold,
                                       &ns_ratio, &ns_attack, &ns_release);
    if (err == ESP_OK) {
        g_dsp_ns_state = ns_enabled;
        ESP_LOGI(TAG, "Noise Suppressor: enabled=%d, threshold=%d.%02d dB, "
                 "ratio=%d, attack=%d ms, release=%d ms",
                 ns_enabled,
                 ns_threshold / 100, abs(ns_threshold) % 100,
                 ns_ratio, ns_attack, ns_release);
    }

    // ---------- 2. Noise Suppressor AUS ----------
    ESP_LOGI(TAG, "Schritt 2: Noise Suppressor AUS-schalten...");
    err = dsp_model_set_noise_suppressor(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NS-Ausschalten fehlgeschlagen: %s", esp_err_to_name(err));
        goto test_end;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    // ---------- 3. Erneut lesen + bestätigen ----------
    ESP_LOGI(TAG, "Schritt 3: Noise Suppressor erneut lesen...");
    mvs_build_query_frame(ns_effect_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        err = usb_host_ctrl_get_report(report, &report_len);
        if (err == ESP_OK && report_len >= 14) {
            mvs_decode_noise_suppressor(report + 5, report_len - 5,
                                         &ns_enabled, NULL, NULL, NULL, NULL);
            if (!ns_enabled) {
                ESP_LOGI(TAG, "✅ BESTÄTIGT: Noise Suppressor ist AUS");
            } else {
                ESP_LOGW(TAG, "⚠️ Noise Suppressor ist noch EIN (Write evtl. ignoriert)");
            }
            g_dsp_ns_state = ns_enabled;
        }
    }

    // ---------- 4. Noise Suppressor EIN ----------
    ESP_LOGI(TAG, "Schritt 4: Noise Suppressor EIN-schalten...");
    err = dsp_model_set_noise_suppressor(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NS-Einschalten fehlgeschlagen: %s", esp_err_to_name(err));
        goto test_end;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    // ---------- 5. Erneut lesen + bestätigen ----------
    ESP_LOGI(TAG, "Schritt 5: Noise Suppressor erneut lesen...");
    mvs_build_query_frame(ns_effect_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        err = usb_host_ctrl_get_report(report, &report_len);
        if (err == ESP_OK && report_len >= 14) {
            mvs_decode_noise_suppressor(report + 5, report_len - 5,
                                         &ns_enabled, NULL, NULL, NULL, NULL);
            if (ns_enabled) {
                ESP_LOGI(TAG, "✅ BESTÄTIGT: Noise Suppressor ist EIN");
            } else {
                ESP_LOGW(TAG, "⚠️ Noise Suppressor ist noch AUS (Write evtl. ignoriert)");
            }
            g_dsp_ns_state = ns_enabled;
        }
    }

    ESP_LOGI(TAG, "=== Noise Suppressor Test abgeschlossen ===");

test_end:
    ESP_LOGI(TAG, "DSP-Test-Task beendet. Noise Suppressor State: %d", g_dsp_ns_state);
    vTaskDelete(NULL);
}
#endif

// ---------------------------------------------------------------------------
// Netzwerk
// ---------------------------------------------------------------------------
static void init_network(void)
{
    // WiFi-Manager initialisieren (WiFi-Stack, Event-Loop, Interfaces)
    esp_err_t wifi_err = wifi_manager_init();
    if (wifi_err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi-Manager-Init fehlgeschlagen: %s", esp_err_to_name(wifi_err));
    }

    // Hostname aus MAC generieren
    char hostname[32];
    wifi_manager_generate_hostname(hostname, sizeof(hostname));

    // mDNS starten
    mdns_service_start(hostname);

    // Gespeicherte WiFi-Zugangsdaten laden
    wifi_creds_t creds;
    esp_err_t err = nvs_settings_load_wifi_creds(&creds);
    bool have_creds = err == ESP_OK && creds.ssid[0] != '\0';

    // Credential-Status im WiFi-Manager setzen (für Lifecycle)
    wifi_manager_set_has_credentials(have_creds);

    device_config_t device_config = {
        .wifi_auto_off = false,
        .wifi_setup_timeout_s = BP10_WIFI_SETUP_TIMEOUT_S,
    };
    if (nvs_settings_load_config(&device_config) != ESP_OK) {
        device_config.wifi_auto_off = false;
        device_config.wifi_setup_timeout_s = BP10_WIFI_SETUP_TIMEOUT_S;
    }
    wifi_manager_configure_auto_off(device_config.wifi_auto_off,
                                    device_config.wifi_setup_timeout_s,
                                    have_creds);

    if (have_creds) {
        // Normaler Boot mit Creds: direkt STA-only, kein AP.
        // AP erscheint nur im Fallback nach 60 s ohne Verbindung.
        ESP_LOGI(TAG, "Gespeicherte WLAN-Zugangsdaten — STA-only Boot, "
                 "verbinde mit %s", creds.ssid);
        wifi_manager_connect_sta(creds.ssid, creds.password);
    } else {
        // Ohne Creds: AP dauerhaft, bis eine Verbindung eingerichtet wird
        ESP_LOGI(TAG, "Keine WLAN-Zugangsdaten — SoftAP (dauerhaft)");
        wifi_manager_start_softap(hostname);
    }

    // Setup-Window: Begrenztes Zeitfenster oder unbegrenzt
    if (BP10_WIFI_SETUP_TIMEOUT_S > 0) {
        ESP_LOGI(TAG, "Setup-Window: %d Sekunden", BP10_WIFI_SETUP_TIMEOUT_S);
    }
}

// ---------------------------------------------------------------------------
// HTTP-Server
// ---------------------------------------------------------------------------
static void init_http_server(void)
{
    ESP_LOGI(TAG, "HTTP-Server starten (Port %d)...", BP10_HTTP_PORT);

    http_server_handle_t server;
    esp_err_t err = http_server_start(&server, BP10_HTTP_PORT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP-Server-Start fehlgeschlagen: %s", esp_err_to_name(err));
        return;
    }

    // REST-API-Handler registrieren
    api_handlers_register(server);

    ESP_LOGI(TAG, "HTTP-Server läuft auf Port %d", BP10_HTTP_PORT);
}
