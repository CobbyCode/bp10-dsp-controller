// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// nvs_settings.h — NVS-Einstellungen (WiFi, DSP-Konfiguration, Gerätename)
//
// v2: Multi-Path-Persistenz (Music + REC getrennt, gemeinsam gespeichert)

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "dsp_model.h"
#include "mvs_device_profile.h"

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

esp_err_t nvs_settings_init(void);
esp_err_t nvs_settings_save_wifi_creds(const wifi_creds_t *creds);
esp_err_t nvs_settings_load_wifi_creds(wifi_creds_t *creds);
esp_err_t nvs_settings_clear_wifi_creds(void);
esp_err_t nvs_settings_save_hostname(const char *hostname);
esp_err_t nvs_settings_load_hostname(char *hostname, size_t max_len);
esp_err_t nvs_settings_save_config(const device_config_t *config);
esp_err_t nvs_settings_load_config(device_config_t *config);

/** Legacy DSP-Konfiguration (Music-Pfad). */
esp_err_t nvs_settings_save_dsp_config(const dsp_profile_t *config);
esp_err_t nvs_settings_load_dsp_config(dsp_profile_t *config);
esp_err_t nvs_settings_clear_dsp_config(void);
bool nvs_settings_has_dsp_config(void);

esp_err_t nvs_settings_factory_reset(void);

// ---------------------------------------------------------------------------
// A800X-Konfiguration
// ---------------------------------------------------------------------------

esp_err_t nvs_settings_save_a800x_config(const dsp_profile_t *config);
esp_err_t nvs_settings_load_a800x_config(dsp_profile_t *config);
bool nvs_settings_has_a800x_config(void);

// ---------------------------------------------------------------------------
// Generic-Konfiguration (Multi-Path, Fingerprint-basiert)
// ---------------------------------------------------------------------------

/**
 * @brief Generic-Multi-Path-Konfiguration speichern.
 *
 * Speichert Fingerprint + dsp_multi_config_t unter Key "dg_<hash>".
 */
esp_err_t nvs_settings_save_generic_config(
    const mvs_schema_fingerprint_t *fp,
    const dsp_multi_config_t *config);

/**
 * @brief Generic-Multi-Path-Konfiguration laden.
 *
 * Lädt Fingerprint + dsp_multi_config_t. Prüft Fingerprint.
 * Bei altem Single-Path-Format: migriert zu Multi-Path (Music).
 *
 * @param fp Erwarteter Fingerprint
 * @param[out] config Ausgabeprofil
 * @return ESP_OK bei Übereinstimmung (inkl. Migration), ESP_ERR_NOT_FOUND bei Mismatch
 */
esp_err_t nvs_settings_load_generic_config(
    const mvs_schema_fingerprint_t *fp,
    dsp_multi_config_t *config);

bool nvs_settings_has_generic_config(const mvs_schema_fingerprint_t *fp);

// ---------------------------------------------------------------------------
// Legacy-Migration
// ---------------------------------------------------------------------------

esp_err_t nvs_settings_migrate_legacy(void);

#ifdef __cplusplus
}
#endif
