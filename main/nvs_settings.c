// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// nvs_settings.c — NVS-Einstellungen
//

#include "nvs_settings.h"
#include "app_config.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "esp_check.h"

static const char *TAG = "a800x_nvs";

// NVS-Handle (global, lazy initialized)
static nvs_handle_t s_nvs_handle = 0;
static bool s_nvs_opened = false;

// NVS-Keys
#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"
#define NVS_KEY_HOSTNAME        "hostname"
#define NVS_KEY_CONFIG          "device_cfg"
#define NVS_KEY_PROFILE_PREFIX  "profile_"

static esp_err_t open_nvs(void)
{
    if (s_nvs_opened) return ESP_OK;

    esp_err_t err = nvs_open(A800X_NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
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

    nvs_set_str(s_nvs_handle, NVS_KEY_CONFIG, (const char *)config);
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

esp_err_t nvs_settings_save_profile(uint8_t index, const dsp_profile_t *profile)
{
    if (!profile || index > A800X_MAX_PROFILES) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    char key[24];
    snprintf(key, sizeof(key), "%s%02x", NVS_KEY_PROFILE_PREFIX, index);

    nvs_set_blob(s_nvs_handle, key, profile, sizeof(dsp_profile_t));
    return nvs_commit(s_nvs_handle);
}

esp_err_t nvs_settings_load_profile(uint8_t index, dsp_profile_t *profile)
{
    if (!profile || index > A800X_MAX_PROFILES) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    memset(profile, 0, sizeof(*profile));
    char key[24];
    snprintf(key, sizeof(key), "%s%02x", NVS_KEY_PROFILE_PREFIX, index);

    size_t len = sizeof(dsp_profile_t);
    return nvs_get_blob(s_nvs_handle, key, profile, &len);
}

esp_err_t nvs_settings_load_active_profile(dsp_profile_t *profile)
{
    return nvs_settings_load_profile(0, profile);
}

esp_err_t nvs_settings_save_active_profile(const dsp_profile_t *profile)
{
    return nvs_settings_save_profile(0, profile);
}

esp_err_t nvs_settings_list_profiles(char names[][A800X_PROFILE_NAME_MAX_LEN],
                                     uint8_t max_count, uint8_t *count)
{
    if (!names || !count) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");

    *count = 0;

    for (uint8_t i = 1; i <= A800X_MAX_PROFILES && *count < max_count; i++) {
        dsp_profile_t profile;
        if (nvs_settings_load_profile(i, &profile) == ESP_OK) {
            if (profile.profile_name[0] != '\0') {
                strncpy(names[*count], profile.profile_name,
                        A800X_PROFILE_NAME_MAX_LEN - 1);
                names[*count][A800X_PROFILE_NAME_MAX_LEN - 1] = '\0';
                (*count)++;
            }
        }
    }

    return ESP_OK;
}

esp_err_t nvs_settings_factory_reset(void)
{
    ESP_RETURN_ON_ERROR(open_nvs(), TAG, "nvs open");
    esp_err_t err = nvs_erase_all(s_nvs_handle);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs_handle);
    }
    return err;
}