// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// nvs_settings.c — NVS-Einstellungen
//
// v2: Multi-Path-Persistenz (Music + REC getrennt, gemeinsam gespeichert)

#include "nvs_settings.h"
#include "app_config.h"
#include <string.h>
#include <stddef.h>
#include "esp_log.h"
#include "nvs.h"
#include "esp_check.h"

static const char *TAG = "bp10_nvs";

static nvs_handle_t s_nvs_handle = 0;
static bool s_nvs_opened = false;

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

#ifndef ESP_ERR_NVS_NOT_FOUND
#define ESP_ERR_NVS_NOT_FOUND ESP_ERR_NOT_FOUND
#endif
#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"
#define NVS_KEY_HOSTNAME        "hostname"
#define NVS_KEY_CONFIG          "device_cfg"
#define NVS_KEY_DSP_CONFIG      "dsp_cfg"
#define NVS_KEY_DSP_A800X       "dsp_a800x"

// Generic-Multi-Path-Blob: Fingerprint + dsp_multi_config_t
#pragma pack(push, 1)
typedef struct {
    mvs_schema_fingerprint_t fingerprint;
    dsp_multi_config_t config;
} nvs_generic_blob_v2_t;

// Legacy Generic-Blob: Fingerprint + dsp_profile_t (v1)
typedef struct {
    mvs_schema_fingerprint_t fingerprint;
    dsp_profile_t config;
} nvs_generic_blob_t;
#pragma pack(pop)

static esp_err_t open_nvs(void)
{
    if (s_nvs_opened) return ESP_OK;
    esp_err_t err = nvs_open(BP10_NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (err == ESP_OK) s_nvs_opened = true;
    return err;
}

// ---------------------------------------------------------------------------
// Öffentliche API
// ---------------------------------------------------------------------------

esp_err_t nvs_settings_init(void)
{
    esp_err_t err = open_nvs();
    if (err == ESP_OK) err = nvs_settings_migrate_legacy();
    return err;
}

// --- WiFi / Hostname / Config (unverändert) ---

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
    esp_err_t err = nvs_get_str(s_nvs_handle, NVS_KEY_WIFI_SSID, creds->ssid, &len);
    if (err != ESP_OK) return err;
    len = sizeof(creds->password);
    return nvs_get_str(s_nvs_handle, NVS_KEY_WIFI_PASS, creds->password, &len);
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
    esp_err_t err = nvs_get_str(s_nvs_handle, NVS_KEY_HOSTNAME, hostname, &len);
    if (err != ESP_OK) hostname[0] = '\0';
    return err;
}

esp_err_t nvs_settings_save_config(const device_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");
    ESP_RETURN_ON_ERROR(nvs_set_blob(s_nvs_handle, NVS_KEY_CONFIG, config, sizeof(*config)), TAG, "config set");
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

// --- Legacy DSP-Konfiguration ---

esp_err_t nvs_settings_save_dsp_config(const dsp_profile_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");
    esp_err_t err = nvs_set_blob(s_nvs_handle, NVS_KEY_DSP_CONFIG, config, sizeof(dsp_profile_t));
    if (err != ESP_OK) { ESP_LOGE(TAG, "DSP save failed: %s", esp_err_to_name(err)); return err; }
    err = nvs_commit(s_nvs_handle);
    if (err == ESP_OK) ESP_LOGI(TAG, "DSP-Konfiguration im NVS gespeichert");
    return err;
}

esp_err_t nvs_settings_load_dsp_config(dsp_profile_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");
    esp_err_t err = load_profile_blob(NVS_KEY_DSP_CONFIG, config);
    if (err == ESP_OK) ESP_LOGI(TAG, "DSP-Konfiguration aus NVS geladen");
    return err;
}

esp_err_t nvs_settings_clear_dsp_config(void)
{
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");
    nvs_erase_key(s_nvs_handle, NVS_KEY_DSP_CONFIG);
    return nvs_commit(s_nvs_handle);
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

// --- Factory Reset ---

esp_err_t nvs_settings_factory_reset(void)
{
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");
    esp_err_t err = nvs_erase_all(s_nvs_handle);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs_handle);
        ESP_LOGI(TAG, "Factory Reset: NVS gelöscht");
    }
    return err;
}

// --- A800X ---

esp_err_t nvs_settings_save_a800x_config(const dsp_profile_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");
    esp_err_t err = nvs_set_blob(s_nvs_handle, NVS_KEY_DSP_A800X, config, sizeof(dsp_profile_t));
    if (err != ESP_OK) { ESP_LOGE(TAG, "A800X save failed: %s", esp_err_to_name(err)); return err; }
    err = nvs_commit(s_nvs_handle);
    if (err == ESP_OK) ESP_LOGI(TAG, "A800X-Konfiguration gespeichert");
    return err;
}

esp_err_t nvs_settings_load_a800x_config(dsp_profile_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");
    esp_err_t err = load_profile_blob(NVS_KEY_DSP_A800X, config);
    if (err == ESP_OK) ESP_LOGI(TAG, "A800X-Konfiguration geladen");
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
// Generic-Multi-Path-Konfiguration
// ---------------------------------------------------------------------------

esp_err_t nvs_settings_save_generic_config(
    const mvs_schema_fingerprint_t *fp,
    const dsp_multi_config_t *config)
{
    if (!fp || !config) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    char nvs_key[12];
    mvs_fingerprint_to_nvs_key(fp, nvs_key, sizeof(nvs_key));

    nvs_generic_blob_v2_t blob;
    blob.fingerprint = *fp;
    blob.config = *config;

    esp_err_t err = nvs_set_blob(s_nvs_handle, nvs_key, &blob, sizeof(blob));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Generic save failed (Key: %s): %s", nvs_key, esp_err_to_name(err));
        return err;
    }
    err = nvs_commit(s_nvs_handle);
    if (err == ESP_OK) ESP_LOGI(TAG, "Generic-Multi-Path gespeichert (Key: %s)", nvs_key);
    return err;
}

esp_err_t nvs_settings_load_generic_config(
    const mvs_schema_fingerprint_t *fp,
    dsp_multi_config_t *config)
{
    if (!fp || !config) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    char nvs_key[12];
    mvs_fingerprint_to_nvs_key(fp, nvs_key, sizeof(nvs_key));
    memset(config, 0, sizeof(*config));

    // Zuerst v2 (Multi-Path) versuchen
    nvs_generic_blob_v2_t blob_v2;
    memset(&blob_v2, 0, sizeof(blob_v2));
    size_t len = sizeof(blob_v2);
    esp_err_t err = nvs_get_blob(s_nvs_handle, nvs_key, &blob_v2, &len);
    if (err == ESP_OK && len >= offsetof(nvs_generic_blob_v2_t, config) + sizeof(dsp_profile_t)) {
        if (!mvs_fingerprint_equal(&blob_v2.fingerprint, fp)) {
            ESP_LOGW(TAG, "Fingerprint-Mismatch bei Generic-Restore (Key: %s)", nvs_key);
            return ESP_ERR_NOT_FOUND;
        }
        *config = blob_v2.config;
        ESP_LOGI(TAG, "Generic-Multi-Path-Konfiguration geladen (Key: %s, v2)", nvs_key);
        return ESP_OK;
    }

    // Fallback: v1 (Single-Path) → Migration
    nvs_generic_blob_t blob_v1;
    memset(&blob_v1, 0, sizeof(blob_v1));
    len = sizeof(blob_v1);
    err = nvs_get_blob(s_nvs_handle, nvs_key, &blob_v1, &len);
    const size_t legacy_blob_size = sizeof(mvs_schema_fingerprint_t) + DSP_PROFILE_LEGACY_SIZE;
    if (err == ESP_OK && len >= legacy_blob_size && len <= sizeof(blob_v1)) {
        if (!mvs_fingerprint_equal(&blob_v1.fingerprint, fp)) {
            ESP_LOGW(TAG, "Fingerprint-Mismatch bei Legacy-Restore (Key: %s)", nvs_key);
            return ESP_ERR_NOT_FOUND;
        }
        // Migration: v1 → v2
        config->schema_version = 2;
        memcpy(&config->music, &blob_v1.config, sizeof(dsp_profile_t));
        config->rec_valid = false;
        ESP_LOGI(TAG, "Generic-Legacy-Konfiguration geladen → als Music migriert (Key: %s)", nvs_key);
        // Save back as v2
        nvs_settings_save_generic_config(fp, config);
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Generic-Konfiguration nicht gefunden (Key: %s)", nvs_key);
    return err == ESP_OK ? ESP_ERR_INVALID_SIZE : (err == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : err);
}

bool nvs_settings_has_generic_config(const mvs_schema_fingerprint_t *fp)
{
    if (!fp || (!s_nvs_opened && open_nvs() != ESP_OK)) return false;

    char nvs_key[12];
    mvs_fingerprint_to_nvs_key(fp, nvs_key, sizeof(nvs_key));

    // v2 prüfen
    nvs_generic_blob_v2_t blob_v2;
    size_t len = sizeof(blob_v2);
    if (nvs_get_blob(s_nvs_handle, nvs_key, &blob_v2, &len) == ESP_OK &&
        len >= offsetof(nvs_generic_blob_v2_t, config) + sizeof(dsp_profile_t) &&
        mvs_fingerprint_equal(&blob_v2.fingerprint, fp))
        return true;

    // v1 prüfen
    nvs_generic_blob_t blob_v1;
    len = sizeof(blob_v1);
    return nvs_get_blob(s_nvs_handle, nvs_key, &blob_v1, &len) == ESP_OK &&
           len >= sizeof(mvs_schema_fingerprint_t) + DSP_PROFILE_LEGACY_SIZE &&
           len <= sizeof(blob_v1) &&
           mvs_fingerprint_equal(&blob_v1.fingerprint, fp);
}

// ---------------------------------------------------------------------------
// Legacy-Migration: "dsp_cfg" → "dsp_a800x"
// ---------------------------------------------------------------------------

esp_err_t nvs_settings_migrate_legacy(void)
{
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    if (nvs_settings_has_a800x_config()) {
        ESP_LOGD(TAG, "Legacy-Migration: A800X-Key existiert bereits");
        return ESP_OK;
    }

    dsp_profile_t legacy;
    memset(&legacy, 0, sizeof(legacy));
    size_t len = sizeof(dsp_profile_t);
    esp_err_t err = nvs_get_blob(s_nvs_handle, NVS_KEY_DSP_CONFIG, &legacy, &len);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Legacy-Migration: kein alter dsp_cfg-Blob");
        return ESP_OK;
    }
    if (len != sizeof(legacy)) {
        ESP_LOGW(TAG, "Legacy-Migration: dsp_cfg hat unerwartete Größe");
        return ESP_ERR_INVALID_SIZE;
    }

    err = nvs_set_blob(s_nvs_handle, NVS_KEY_DSP_A800X, &legacy, sizeof(dsp_profile_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Legacy-Migration: A800X-Key speichern fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }
    nvs_erase_key(s_nvs_handle, NVS_KEY_DSP_CONFIG);
    err = nvs_commit(s_nvs_handle);
    if (err == ESP_OK) ESP_LOGI(TAG, "Legacy-Migration: dsp_cfg → dsp_a800x migriert");
    return err;
}
