// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// nvs_settings.h — NVS-Einstellungen (WiFi, DSP-Konfiguration, Gerätename)
//

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "dsp_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BP10_WIFI_SSID_MAX_LEN    32
#define BP10_WIFI_PASS_MAX_LEN    64
#define BP10_HOSTNAME_MAX_LEN     32

// WiFi-Zugangsdaten
typedef struct {
    char ssid[BP10_WIFI_SSID_MAX_LEN];
    char password[BP10_WIFI_PASS_MAX_LEN];
} wifi_creds_t;

// Gerätekonfiguration
typedef struct {
    char hostname[BP10_HOSTNAME_MAX_LEN];
    bool wifi_auto_off;
    uint32_t wifi_setup_timeout_s;
} device_config_t;

/**
 * @brief NVS initialisieren (Namespace bp10).
 */
esp_err_t nvs_settings_init(void);

/**
 * @brief WiFi-Zugangsdaten speichern.
 */
esp_err_t nvs_settings_save_wifi_creds(const wifi_creds_t *creds);

/**
 * @brief WiFi-Zugangsdaten laden.
 */
esp_err_t nvs_settings_load_wifi_creds(wifi_creds_t *creds);

/**
 * @brief WiFi-Zugangsdaten löschen (Factory Reset).
 */
esp_err_t nvs_settings_clear_wifi_creds(void);

/**
 * @brief Gerätename speichern.
 */
esp_err_t nvs_settings_save_hostname(const char *hostname);

/**
 * @brief Gerätename laden.
 */
esp_err_t nvs_settings_load_hostname(char *hostname, size_t max_len);

/**
 * @brief Gerätekonfiguration speichern.
 */
esp_err_t nvs_settings_save_config(const device_config_t *config);

/**
 * @brief Gerätekonfiguration laden.
 */
esp_err_t nvs_settings_load_config(device_config_t *config);

/**
 * @brief DSP-Konfiguration im NVS speichern (wird bei jedem erfolgreichen
 *        Apply automatisch gespeichert; beim Boot/DSP-Reconnect angewendet).
 */
esp_err_t nvs_settings_save_dsp_config(const dsp_profile_t *config);

/**
 * @brief DSP-Konfiguration aus NVS laden.
 *
 * @return ESP_OK wenn Konfiguration vorhanden, ESP_ERR_NOT_FOUND wenn keine
 *         gespeichert ist.
 */
esp_err_t nvs_settings_load_dsp_config(dsp_profile_t *config);

/**
 * @brief DSP-Konfiguration aus NVS löschen.
 */
esp_err_t nvs_settings_clear_dsp_config(void);

/**
 * @brief Prüft, ob eine DSP-Konfiguration im NVS gespeichert ist.
 */
bool nvs_settings_has_dsp_config(void);

/**
 * @brief Alle Einstellungen löschen (Factory Reset).
 *
 * Löscht NVS (WiFi, DSP-Konfiguration, Gerätename).
 * Sendet KEIN 0xFD an den DSP.
 */
esp_err_t nvs_settings_factory_reset(void);

#ifdef __cplusplus
}
#endif
