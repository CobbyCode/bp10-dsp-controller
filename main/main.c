// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// main.c — AIYIMA A800X DSP Controller — Einstiegspunkt
//
// Initialisierungsreihenfolge:
// 1. NVS
// 2. USB-Host starten (nicht blockierend)
// 3. Netzwerk: WiFi + mDNS + HTTP-Server
// 4. DSP-Init: optional, nur wenn USB-Device erkannt
// 5. Normalbetrieb: Web-UI, Noise-Suppressor-Test, OTA, Config-IO
//
// Wichtig: Das System startet auch ohne angeschlossenen A800X.
// Die Web-UI zeigt dann "DSP nicht verfügbar".
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
#include "nvs_settings.h"
#include "wifi_manager.h"
#include "mdns_service.h"
#include "http_server.h"
#include "api_handlers.h"
#include "ota_update.h"
#include "config_io.h"

static const char *TAG = "a800x_main";

// Globaler DSP-Status (für API/UI)
bool g_dsp_connected = false;
bool g_dsp_ns_state = false;  // Noise Suppressor gelesener Zustand

// Forward declarations
static void init_nvs(void);
static void init_usb_host(void);
static void init_network(void);
static void init_http_server(void);
static void apply_dsp_profile(void);
static void dsp_test_task(void *arg);

void app_main(void)
{
    ESP_LOGI(TAG, "=== AIYIMA A800X DSP Controller ===");
    ESP_LOGI(TAG, "Firmware version: " APP_VERSION);

    // 1. NVS initialisieren
    init_nvs();

    // 2. USB-Host starten (nicht blockierend, timeout 5 s)
    init_usb_host();

    // 3. Netzwerk starten: WiFi + mDNS + HTTP
    //    Läuft immer, auch ohne DSP
    init_network();
    init_http_server();

    // 4. DSP-Profil anwenden (nur wenn Device erkannt)
    if (g_dsp_connected) {
        apply_dsp_profile();

        // 5. DSP-Test-Task starten (Noise Suppressor Read/Write-Zyklus)
        xTaskCreatePinnedToCore(
            dsp_test_task,
            "dsp_test",
            4096,
            NULL,
            3,
            NULL,
            1  // Core 1
        );
    }

    ESP_LOGI(TAG, "Initialisierung abgeschlossen.");
    ESP_LOGI(TAG, "Web-UI: http://%s.local", CONFIG_A800X_MDNS_DEFAULT_HOSTNAME);

    // Normalbetrieb: Status-Loop
    while (1) {
        // DSP-Connected-Status aktualisieren (für API)
        g_dsp_connected = usb_host_ctrl_is_device_connected();
        vTaskDelay(pdMS_TO_TICKS(5000));
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
    ESP_LOGI(TAG, "USB-Host initialisieren...");

    // VBUS einschalten (falls konfiguriert)
    usb_host_vbus_enable(true);

    // USB-Host initialisieren
    esp_err_t err = usb_host_ctrl_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB-Host-Init fehlgeschlagen: %s", esp_err_to_name(err));
        g_dsp_connected = false;
        return;
    }

    // Auf DSP-Device warten (Timeout via Kconfig)
    err = usb_host_ctrl_wait_for_device(A800X_USB_VID, A800X_USB_PID,
                                        A800X_USB_DEVICE_WAIT_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MVSilicon-Gerät nicht gefunden. System läuft ohne DSP.");
        g_dsp_connected = false;
        return;
    }

    g_dsp_connected = true;
    ESP_LOGI(TAG, "MVSilicon-Gerät VID:0x%04X PID:0x%04X erkannt",
             A800X_USB_VID, A800X_USB_PID);
}

// ---------------------------------------------------------------------------
// DSP-Profil anwenden (nur bei verbundenem Device)
// ---------------------------------------------------------------------------
static void apply_dsp_profile(void)
{
    ESP_LOGI(TAG, "DSP-Profil wiederherstellen...");

    dsp_profile_t profile;
    esp_err_t err = nvs_settings_load_active_profile(&profile);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Kein gespeichertes Profil, Standard verwenden");
        dsp_model_get_default_profile(&profile);
    }

    err = dsp_model_apply_profile(&profile);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Profil anwenden fehlgeschlagen: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "DSP-Profil angewendet");
    }

    // Wichtig: KEIN 0xFD Flash-Save senden!
    ESP_LOGI(TAG, "Hinweis: Kein DSP-Flash-Save (0xFD) — ESP stellt Parameter nach jedem Einschalten wieder her.");
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
static void dsp_test_task(void *arg)
{
    ESP_LOGI(TAG, "DSP-Test-Task gestartet");
    vTaskDelay(pdMS_TO_TICKS(2000));  // Warten auf USB-Stabilität

    uint8_t frame[16];
    uint8_t report[256];
    uint16_t report_len;
    esp_err_t err;

    // ---------- 1. Noise Suppressor lesen ----------
    ESP_LOGI(TAG, "=== Noise Suppressor Test ===");
    ESP_LOGI(TAG, "Schritt 1: Noise Suppressor lesen...");

    mvs_build_query_frame(MVS_EFFECT_NOISE_SUPPRESSOR, frame, sizeof(frame));
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
    err = mvs_decode_noise_suppressor(report + 4, report_len - 4,
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
    mvs_build_query_frame(MVS_EFFECT_NOISE_SUPPRESSOR, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        err = usb_host_ctrl_get_report(report, &report_len);
        if (err == ESP_OK && report_len >= 14) {
            mvs_decode_noise_suppressor(report + 4, report_len - 4,
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
    mvs_build_query_frame(MVS_EFFECT_NOISE_SUPPRESSOR, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        err = usb_host_ctrl_get_report(report, &report_len);
        if (err == ESP_OK && report_len >= 14) {
            mvs_decode_noise_suppressor(report + 4, report_len - 4,
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

    if (err == ESP_OK && creds.ssid[0] != '\0') {
        ESP_LOGI(TAG, "Gespeicherte WLAN-Zugangsdaten gefunden, verbinde...");
        wifi_manager_connect_sta(creds.ssid, creds.password);
    } else {
        ESP_LOGI(TAG, "Keine WLAN-Zugangsdaten — SoftAP-Modus starten");
        wifi_manager_start_softap(hostname);
    }

    // Setup-Window: Begrenztes Zeitfenster oder unbegrenzt
    if (A800X_WIFI_SETUP_TIMEOUT_S > 0) {
        ESP_LOGI(TAG, "Setup-Window: %d Sekunden", A800X_WIFI_SETUP_TIMEOUT_S);
    }
}

// ---------------------------------------------------------------------------
// HTTP-Server
// ---------------------------------------------------------------------------
static void init_http_server(void)
{
    ESP_LOGI(TAG, "HTTP-Server starten (Port %d)...", A800X_HTTP_PORT);

    http_server_handle_t server;
    esp_err_t err = http_server_start(&server, A800X_HTTP_PORT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP-Server-Start fehlgeschlagen: %s", esp_err_to_name(err));
        return;
    }

    // REST-API-Handler registrieren
    api_handlers_register(server);

    ESP_LOGI(TAG, "HTTP-Server läuft auf Port %d", A800X_HTTP_PORT);
}
