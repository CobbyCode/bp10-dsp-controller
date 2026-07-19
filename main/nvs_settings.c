// SPDX-FileCopyrightText: 2026 CobbyCode
// SPDX-License-Identifier: GPL-3.0-or-later
//
// nvs_settings.c — NVS-Einstellungen
//

#include "nvs_settings.h"
#include "app_config.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "esp_check.h"

static const char *TAG = "bp10_nvs";

// NVS-Handle (global, lazy initialized)
static nvs_handle_t s_nvs_handle = 0;
static bool s_nvs_opened = false;

// NVS-Keys
#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"
#define NVS_KEY_HOSTNAME        "hostname"
#define NVS_KEY_CONFIG          "device_cfg"
#define NVS_KEY_DSP_CONFIG      "dsp_cfg"

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
    return open_nvs();
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

    memset(config, 0, sizeof(*config));
    size_t len = sizeof(dsp_profile_t);
    esp_err_t err = nvs_get_blob(s_nvs_handle, NVS_KEY_DSP_CONFIG,
                                 config, &len);
    if (err == ESP_OK && len == sizeof(dsp_profile_t)) {
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
           && len == sizeof(dsp_profile_t);
}

// ---------------------------------------------------------------------------
// Factory Reset
// ---------------------------------------------------------------------------

esp_err_t nvs_settings_factory_reset(void)
{
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");
    esp_err_t err = nvs_erase_all(s_nvs_handle);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs_handle);
        ESP_LOGI(TAG, "Factory Reset: NVS gelöscht (kein DSP-Flash-Save)");
    }
    return err;
}
