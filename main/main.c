// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// main.c — AIYIMA A800X DSP Controller — Einstiegspunkt
//
// Initialisierungsreihenfolge:
// 1. NVS
// 2. USB-Host + DSP-Device-Enumeration
// 3. DSP-Parameter wiederherstellen (kein Flash-Save)
// 4. Netzwerk: WiFi + mDNS + HTTP-Server
// 5. Setup-Window (begrenzt oder unbegrenzt)
// 6. Normalbetrieb: Web-UI, OTA, Config-IO
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

// Forward declarations
static void init_nvs(void);
static void init_usb_host(void);
static void init_network(void);
static void init_http_server(void);
static void apply_dsp_profile(void);

void app_main(void)
{
    ESP_LOGI(TAG, "=== AIYIMA A800X DSP Controller ===");
    ESP_LOGI(TAG, "Firmware version: " APP_VERSION);

    // 1. NVS initialisieren
    init_nvs();

    // 2. USB-Host initialisieren und DSP-Gerät erkennen
    init_usb_host();

    // 3. DSP-Parameter aus Profil wiederherstellen
    //    (Kein 0xFD Flash-Save!)
    apply_dsp_profile();

    // 4. Netzwerk starten: WiFi + mDNS + HTTP
    init_network();
    init_http_server();

    ESP_LOGI(TAG, "Initialisierung abgeschlossen. Setup-Window aktiv.");
    ESP_LOGI(TAG, "Web-UI: http://%s.local", CONFIG_A800X_MDNS_DEFAULT_HOSTNAME);

    // Im Normalbetrieb bleibt die Haupt-Task am Leben und
    // überwacht den Systemzustand.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
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
// USB-Host
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
        return;
    }

    // Auf DSP-Device warten
    err = usb_host_ctrl_wait_for_device(A800X_USB_VID, A800X_USB_PID,
                                        A800X_USB_DEVICE_WAIT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MVSilicon-Gerät nicht gefunden: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "MVSilicon-Gerät VID:0x%04X PID:0x%04X erkannt",
             A800X_USB_VID, A800X_USB_PID);
}

// ---------------------------------------------------------------------------
// DSP-Profil anwenden
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
    // Der A800X speichert DSP-Parameter nicht dauerhaft.
    // Der ESP stellt die Parameter nach jedem Power-On wieder her.
    ESP_LOGI(TAG, "Hinweis: Kein DSP-Flash-Save (0xFD) — ESP stellt Parameter nach jedem Einschalten wieder her.");
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
        // Nach Ablauf des Timeouts: WLAN deaktivieren, falls kein AP mehr nötig
        // (wird vom wifi_manager gehandhabt)
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