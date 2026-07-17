// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// config_io.c — Konfigurations-Import/Export
//

#include "config_io.h"
#include "dsp_model.h"
#include "nvs_settings.h"
#include "ota_update.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "a800x_config";

// ---------------------------------------------------------------------------
// Öffentliche API
// ---------------------------------------------------------------------------

esp_err_t config_io_export(char **json)
{
    if (!json) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    // Version
    cJSON_AddStringToObject(root, "version", "1.0");
    cJSON_AddStringToObject(root, "app_version", APP_VERSION);

    // Gerätename
    char hostname[32] = {0};
    if (nvs_settings_load_hostname(hostname, sizeof(hostname)) == ESP_OK) {
        cJSON_AddStringToObject(root, "hostname", hostname);
    }

    // WiFi-Zugangsdaten
    wifi_creds_t creds;
    if (nvs_settings_load_wifi_creds(&creds) == ESP_OK) {
        cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
        cJSON_AddStringToObject(wifi, "ssid", creds.ssid);
        cJSON_AddStringToObject(wifi, "password", creds.password);
    }

    // DSP-Profil
    dsp_profile_t profile;
    if (nvs_settings_load_active_profile(&profile) == ESP_OK) {
        cJSON *dsp = cJSON_AddObjectToObject(root, "dsp");
        cJSON_AddBoolToObject(dsp, "noise_suppressor_enabled",
                              profile.noise_suppressor_enabled);
        cJSON_AddBoolToObject(dsp, "virtual_bass_enabled",
                              profile.virtual_bass_enabled);
        cJSON_AddBoolToObject(dsp, "silence_detector_enabled",
                              profile.silence_detector_enabled);
        cJSON_AddBoolToObject(dsp, "preeq_enabled",
                              profile.preeq.block_enabled != 0);
        cJSON_AddBoolToObject(dsp, "drc_enabled",
                              profile.drc.enabled != 0);
    }

    *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!*json) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t config_io_import(const char *json)
{
    if (!json) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "Ungültiges JSON");
        return ESP_ERR_INVALID_ARG;
    }

    // Hostname
    cJSON *hostname = cJSON_GetObjectItem(root, "hostname");
    if (cJSON_IsString(hostname) && strlen(hostname->valuestring) > 0) {
        nvs_settings_save_hostname(hostname->valuestring);
    }

    // WiFi
    cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
    if (cJSON_IsObject(wifi)) {
        wifi_creds_t creds;
        cJSON *ssid = cJSON_GetObjectItem(wifi, "ssid");
        cJSON *pass = cJSON_GetObjectItem(wifi, "password");
        if (cJSON_IsString(ssid)) {
            strncpy(creds.ssid, ssid->valuestring, sizeof(creds.ssid) - 1);
            if (cJSON_IsString(pass)) {
                strncpy(creds.password, pass->valuestring,
                        sizeof(creds.password) - 1);
            } else {
                creds.password[0] = '\0';
            }
            nvs_settings_save_wifi_creds(&creds);
        }
    }

    // DSP-Profil
    cJSON *dsp = cJSON_GetObjectItem(root, "dsp");
    if (cJSON_IsObject(dsp)) {
        dsp_profile_t profile;
        dsp_model_get_default_profile(&profile);

        cJSON *item;
        item = cJSON_GetObjectItem(dsp, "noise_suppressor_enabled");
        if (cJSON_IsBool(item)) profile.noise_suppressor_enabled = cJSON_IsTrue(item);
        item = cJSON_GetObjectItem(dsp, "virtual_bass_enabled");
        if (cJSON_IsBool(item)) profile.virtual_bass_enabled = cJSON_IsTrue(item);
        item = cJSON_GetObjectItem(dsp, "silence_detector_enabled");
        if (cJSON_IsBool(item)) profile.silence_detector_enabled = cJSON_IsTrue(item);
        item = cJSON_GetObjectItem(dsp, "preeq_enabled");
        if (cJSON_IsBool(item)) profile.preeq.block_enabled = cJSON_IsTrue(item) ? 1 : 0;
        item = cJSON_GetObjectItem(dsp, "drc_enabled");
        if (cJSON_IsBool(item)) profile.drc.enabled = cJSON_IsTrue(item) ? 1 : 0;

        nvs_settings_save_active_profile(&profile);
        dsp_model_apply_profile(&profile);
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Konfiguration importiert");
    return ESP_OK;
}