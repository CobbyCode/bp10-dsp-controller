// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// nvs_settings.c — NVS-Einstellungen
//

#include "nvs_settings.h"
#include "app_config.h"
#include <string.h>
#include <stddef.h>
#include "esp_log.h"
#include "nvs.h"
#include "esp_check.h"

static const char *TAG = "bp10_nvs";

// NVS-Handle (global, lazy initialized)
static nvs_handle_t s_nvs_handle = 0;
static bool s_nvs_opened = false;

// Profiles up to 55ea822 end immediately before delay_enabled. New fields are
// appended only, so zero-fill + prefix copy safely loads those existing blobs.
#define DSP_PROFILE_LEGACY_SIZE offsetof(dsp_profile_t, delay_enabled)

static esp_err_t load_profile_blob(const char *key, dsp_profile_t *config)
{
    memset(config, 0, sizeof(*config));
    size_t len = sizeof(*config);
    esp_err_t err = nvs_get_blob(s_nvs_handle, key, config, &len);
    if (err != ESP_OK) return err;
    return (len >= DSP_PROFILE_LEGACY_SIZE && len <= sizeof(*config))
        ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

// NVS-Keys
#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"
#define NVS_KEY_HOSTNAME        "hostname"
#define NVS_KEY_CONFIG          "device_cfg"
#define NVS_KEY_DSP_CONFIG      "dsp_cfg"     // Legacy (≤ 0.4.0)
#define NVS_KEY_DSP_A800X       "dsp_a800x"   // A800X-Festprofil
// Generic-Key: "dg_" + 8 Hex-Zeichen CRC32 (11 Zeichen, passt in NVS 15 Limit)

// Generic-Blob: Fingerprint + dsp_profile_t
#pragma pack(push, 1)
typedef struct {
    mvs_schema_fingerprint_t fingerprint;
    dsp_profile_t config;
} nvs_generic_blob_t;
#pragma pack(pop)

static esp_err_t open_nvs(void)
{
    if (s_nvs_opened) return ESP_OK;

    esp_err_t err = nvs_open(BP10_NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (err == ESP_OK) {
        s_nvs_opened = true;
    }
    return err;
}

// ---------------------------------------------------------------------------
// Öffentliche API
// ---------------------------------------------------------------------------

esp_err_t nvs_settings_init(void)
{
    esp_err_t err = open_nvs();
    if (err == ESP_OK) {
        // Einmalige Legacy-Migration: dsp_cfg → dsp_a800x
        err = nvs_settings_migrate_legacy();
    }
    return err;
}

esp_err_t nvs_settings_save_wifi_creds(const wifi_creds_t *creds)
{
    if (!creds) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    nvs_set_str(s_nvs_handle, NVS_KEY_WIFI_SSID, creds->ssid);
    nvs_set_str(s_nvs_handle, NVS_KEY_WIFI_PASS, creds->password);
    return nvs_commit(s_nvs_handle);
}

esp_err_t nvs_settings_load_wifi_creds(wifi_creds_t *creds)
{
    if (!creds) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    memset(creds, 0, sizeof(*creds));

    size_t len = sizeof(creds->ssid);
    esp_err_t err = nvs_get_str(s_nvs_handle, NVS_KEY_WIFI_SSID,
                                creds->ssid, &len);
    if (err != ESP_OK) return err;

    len = sizeof(creds->password);
    return nvs_get_str(s_nvs_handle, NVS_KEY_WIFI_PASS,
                       creds->password, &len);
}

esp_err_t nvs_settings_clear_wifi_creds(void)
{
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");
    nvs_erase_key(s_nvs_handle, NVS_KEY_WIFI_SSID);
    nvs_erase_key(s_nvs_handle, NVS_KEY_WIFI_PASS);
    return nvs_commit(s_nvs_handle);
}

esp_err_t nvs_settings_save_hostname(const char *hostname)
{
    if (!hostname) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    nvs_set_str(s_nvs_handle, NVS_KEY_HOSTNAME, hostname);
    return nvs_commit(s_nvs_handle);
}

esp_err_t nvs_settings_load_hostname(char *hostname, size_t max_len)
{
    if (!hostname || max_len == 0) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    size_t len = max_len;
    esp_err_t err = nvs_get_str(s_nvs_handle, NVS_KEY_HOSTNAME,
                                hostname, &len);
    if (err != ESP_OK) {
        hostname[0] = '\0';
    }
    return err;
}

esp_err_t nvs_settings_save_config(const device_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    ESP_RETURN_ON_ERROR(nvs_set_blob(s_nvs_handle, NVS_KEY_CONFIG,
                                     config, sizeof(*config)), TAG, "config set");
    return nvs_commit(s_nvs_handle);
}

esp_err_t nvs_settings_load_config(device_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    memset(config, 0, sizeof(*config));
    size_t len = sizeof(*config);
    return nvs_get_blob(s_nvs_handle, NVS_KEY_CONFIG, config, &len);
}

// ---------------------------------------------------------------------------
// DSP-Konfiguration (eine aktive Konfiguration, kein Profil-System)
// ---------------------------------------------------------------------------

esp_err_t nvs_settings_save_dsp_config(const dsp_profile_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    esp_err_t err = nvs_set_blob(s_nvs_handle, NVS_KEY_DSP_CONFIG,
                                 config, sizeof(dsp_profile_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DSP-Konfiguration speichern fehlgeschlagen: %s",
                 esp_err_to_name(err));
        return err;
    }
    err = nvs_commit(s_nvs_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "DSP-Konfiguration im NVS gespeichert");
    }
    return err;
}

esp_err_t nvs_settings_load_dsp_config(dsp_profile_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    esp_err_t err = load_profile_blob(NVS_KEY_DSP_CONFIG, config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "DSP-Konfiguration aus NVS geladen");
    }
    return err;
}

esp_err_t nvs_settings_clear_dsp_config(void)
{
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");
    nvs_erase_key(s_nvs_handle, NVS_KEY_DSP_CONFIG);
    esp_err_t err = nvs_commit(s_nvs_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "DSP-Konfiguration aus NVS gelöscht");
    }
    return err;
}

bool nvs_settings_has_dsp_config(void)
{
    if (!s_nvs_opened && open_nvs() != ESP_OK) return false;

    dsp_profile_t config;
    memset(&config, 0, sizeof(config));
    size_t len = sizeof(config);
    return nvs_get_blob(s_nvs_handle, NVS_KEY_DSP_CONFIG, &config, &len) == ESP_OK
           && len >= DSP_PROFILE_LEGACY_SIZE && len <= sizeof(dsp_profile_t);
}

// ---------------------------------------------------------------------------
// Factory Reset
// ---------------------------------------------------------------------------

esp_err_t nvs_settings_factory_reset(void)
{
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");
    // nvs_erase_all löscht ALLE Keys in der Namespace:
    // dsp_a800x, dg_*, dsp_cfg (legacy), WiFi, hostname, device_cfg
    esp_err_t err = nvs_erase_all(s_nvs_handle);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs_handle);
        ESP_LOGI(TAG, "Factory Reset: NVS gelöscht (A800X + Generic + WiFi + Config, kein DSP-Flash-Save)");
    }
    return err;
}

// ---------------------------------------------------------------------------
// A800X-Konfiguration (fester NVS-Key "dsp_a800x")
// ---------------------------------------------------------------------------

esp_err_t nvs_settings_save_a800x_config(const dsp_profile_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    esp_err_t err = nvs_set_blob(s_nvs_handle, NVS_KEY_DSP_A800X,
                                 config, sizeof(dsp_profile_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "A800X-Konfiguration speichern fehlgeschlagen: %s",
                 esp_err_to_name(err));
        return err;
    }
    err = nvs_commit(s_nvs_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "A800X-Konfiguration im NVS gespeichert (Key: %s)", NVS_KEY_DSP_A800X);
    }
    return err;
}

esp_err_t nvs_settings_load_a800x_config(dsp_profile_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    esp_err_t err = load_profile_blob(NVS_KEY_DSP_A800X, config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "A800X-Konfiguration aus NVS geladen");
    }
    return err;
}

bool nvs_settings_has_a800x_config(void)
{
    if (!s_nvs_opened && open_nvs() != ESP_OK) return false;

    dsp_profile_t config;
    memset(&config, 0, sizeof(config));
    size_t len = sizeof(config);
    return nvs_get_blob(s_nvs_handle, NVS_KEY_DSP_A800X, &config, &len) == ESP_OK
           && len >= DSP_PROFILE_LEGACY_SIZE && len <= sizeof(dsp_profile_t);
}

// ---------------------------------------------------------------------------
// Generic-Konfiguration (Fingerprint-basierter NVS-Key "dg_<hash>")
// ---------------------------------------------------------------------------

esp_err_t nvs_settings_save_generic_config(
    const mvs_schema_fingerprint_t *fp,
    const dsp_profile_t *config)
{
    if (!fp || !config) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    char nvs_key[12];
    mvs_fingerprint_to_nvs_key(fp, nvs_key, sizeof(nvs_key));

    nvs_generic_blob_t blob;
    blob.fingerprint = *fp;
    blob.config = *config;

    esp_err_t err = nvs_set_blob(s_nvs_handle, nvs_key,
                                 &blob, sizeof(blob));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Generic-Konfiguration speichern fehlgeschlagen (Key: %s): %s",
                 nvs_key, esp_err_to_name(err));
        return err;
    }
    err = nvs_commit(s_nvs_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Generic-Konfiguration im NVS gespeichert (Key: %s)", nvs_key);
    }
    return err;
}

esp_err_t nvs_settings_load_generic_config(
    const mvs_schema_fingerprint_t *fp,
    dsp_profile_t *config)
{
    if (!fp || !config) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    char nvs_key[12];
    mvs_fingerprint_to_nvs_key(fp, nvs_key, sizeof(nvs_key));

    nvs_generic_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    size_t len = sizeof(blob);
    esp_err_t err = nvs_get_blob(s_nvs_handle, nvs_key, &blob, &len);
    const size_t legacy_blob_size = sizeof(mvs_schema_fingerprint_t) +
                                    DSP_PROFILE_LEGACY_SIZE;
    if (err != ESP_OK || len < legacy_blob_size || len > sizeof(blob)) {
        ESP_LOGD(TAG, "Generic-Konfiguration nicht gefunden (Key: %s)", nvs_key);
        return err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
    }

    // Fingerprint-Vergleich: gespeicherter vs. erwarteter
    if (!mvs_fingerprint_equal(&blob.fingerprint, fp)) {
        ESP_LOGW(TAG, "Fingerprint-Mismatch bei Generic-Restore (Key: %s) – keine DSP-Writes!",
                 nvs_key);
        return ESP_ERR_NOT_FOUND;
    }

    *config = blob.config;
    ESP_LOGI(TAG, "Generic-Konfiguration aus NVS geladen (Key: %s, Fingerprint OK)", nvs_key);
    return ESP_OK;
}

bool nvs_settings_has_generic_config(const mvs_schema_fingerprint_t *fp)
{
    if (!fp) return false;
    if (!s_nvs_opened && open_nvs() != ESP_OK) return false;

    char nvs_key[12];
    mvs_fingerprint_to_nvs_key(fp, nvs_key, sizeof(nvs_key));

    nvs_generic_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    size_t len = sizeof(blob);
    if (nvs_get_blob(s_nvs_handle, nvs_key, &blob, &len) != ESP_OK ||
        len < sizeof(mvs_schema_fingerprint_t) + DSP_PROFILE_LEGACY_SIZE ||
        len > sizeof(blob))
        return false;

    return mvs_fingerprint_equal(&blob.fingerprint, fp);
}

// ---------------------------------------------------------------------------
// Legacy-Migration: "dsp_cfg" → "dsp_a800x"
//
// Generic-Persistence war in 0.4.0 deaktiviert, daher darf der alte Blob
// bedenkenlos als A800X migriert werden.
// ---------------------------------------------------------------------------

esp_err_t nvs_settings_migrate_legacy(void)
{
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    // Wenn bereits eine A800X-Konfiguration existiert, nichts migrieren
    if (nvs_settings_has_a800x_config()) {
        ESP_LOGD(TAG, "Legacy-Migration: A800X-Key existiert bereits – übersprungen");
        return ESP_OK;
    }

    // Prüfen ob alter Key existiert
    dsp_profile_t legacy;
    memset(&legacy, 0, sizeof(legacy));
    size_t len = sizeof(dsp_profile_t);
    esp_err_t err = nvs_get_blob(s_nvs_handle, NVS_KEY_DSP_CONFIG,
                                 &legacy, &len);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Legacy-Migration: kein alter dsp_cfg-Blob gefunden");
        return ESP_OK;  // nichts zu migrieren
    }
    if (len != sizeof(legacy)) {
        ESP_LOGW(TAG, "Legacy-Migration: dsp_cfg hat unerwartete Größe");
        return ESP_ERR_INVALID_SIZE;
    }

    // Alten Blob als A800X speichern
    err = nvs_set_blob(s_nvs_handle, NVS_KEY_DSP_A800X,
                       &legacy, sizeof(dsp_profile_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Legacy-Migration: A800X-Key speichern fehlgeschlagen: %s",
                 esp_err_to_name(err));
        return err;
    }

    // Alten Key löschen
    nvs_erase_key(s_nvs_handle, NVS_KEY_DSP_CONFIG);
    err = nvs_commit(s_nvs_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Legacy-Migration: dsp_cfg → dsp_a800x migriert");
    }
    return err;
}
