// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// nvs_settings.h — NVS-Einstellungen (WiFi, Profile, Gerätename)
//

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "dsp_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define A800X_WIFI_SSID_MAX_LEN    32
#define A800X_WIFI_PASS_MAX_LEN    64
#define A800X_HOSTNAME_MAX_LEN     32
#define A800X_PROFILE_NAME_MAX_LEN 32

// WiFi-Zugangsdaten
typedef struct {
    char ssid[A800X_WIFI_SSID_MAX_LEN];
    char password[A800X_WIFI_PASS_MAX_LEN];
} wifi_creds_t;

// Gerätekonfiguration
typedef struct {
    char hostname[A800X_HOSTNAME_MAX_LEN];
    bool wifi_auto_off;
    uint32_t wifi_setup_timeout_s;
} device_config_t;

/**
 * @brief NVS initialisieren (Namespace a800x).
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
 * @brief DSP-Profil speichern.
 *
 * @param index Profil-Index (0 = active, 1..A800X_MAX_PROFILES)
 * @param profile DSP-Profil
 */
esp_err_t nvs_settings_save_profile(uint8_t index, const dsp_profile_t *profile);

/**
 * @brief DSP-Profil laden.
 *
 * @param index Profil-Index
 * @param[out] profile DSP-Profil
 */
esp_err_t nvs_settings_load_profile(uint8_t index, dsp_profile_t *profile);

/**
 * @brief Aktives Profil laden (Alias für Index 0).
 */
esp_err_t nvs_settings_load_active_profile(dsp_profile_t *profile);

/**
 * @brief Aktives Profil speichern.
 */
esp_err_t nvs_settings_save_active_profile(const dsp_profile_t *profile);

/**
 * @brief Profil-Liste abrufen (Namen der gespeicherten Profile).
 *
 * @param names Array von Namen
 * @param max_count Maximale Anzahl
 * @param[out] count Gefundene Profile
 */
esp_err_t nvs_settings_list_profiles(char names[][A800X_PROFILE_NAME_MAX_LEN],
                                     uint8_t max_count, uint8_t *count);

/**
 * @brief Alle Einstellungen löschen (Factory Reset).
 */
esp_err_t nvs_settings_factory_reset(void);

#ifdef __cplusplus
}
#endif