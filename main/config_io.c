// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// config_io.c — DSP-Konfiguration Import/Export (JSON)
//
// Exportiert/importiert NUR die DSP-Konfiguration.
// Keine WiFi-Zugangsdaten, Passwörter, IPs, MAC oder Gerätenamen.
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
// Export
// ---------------------------------------------------------------------------

esp_err_t config_io_export(char **json)
{
    if (!json) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    // Schema & Version
    cJSON_AddNumberToObject(root, "schema_version", DSP_CONFIG_SCHEMA_VERSION);
    cJSON_AddStringToObject(root, "app_version", APP_VERSION);
    cJSON_AddStringToObject(root, "type", "a800x-dsp-config");

    // DSP-Konfiguration aus NVS laden
    dsp_profile_t config;
    esp_err_t err = nvs_settings_load_dsp_config(&config);
    if (err == ESP_OK) {
        cJSON *dsp = cJSON_AddObjectToObject(root, "dsp");

        // Noise Suppressor
        cJSON *ns = cJSON_AddObjectToObject(dsp, "noise_suppressor");
        cJSON_AddBoolToObject(ns, "enabled", config.noise_suppressor_enabled);
        cJSON_AddNumberToObject(ns, "threshold_db",
                                config.noise_suppressor_threshold_raw / 100.0);
        cJSON_AddNumberToObject(ns, "ratio", config.noise_suppressor_ratio);
        cJSON_AddNumberToObject(ns, "attack_ms", config.noise_suppressor_attack_ms);
        cJSON_AddNumberToObject(ns, "release_ms", config.noise_suppressor_release_ms);

        // Virtual Bass
        cJSON *vb = cJSON_AddObjectToObject(dsp, "virtual_bass");
        cJSON_AddBoolToObject(vb, "enabled", config.virtual_bass_enabled);
        cJSON_AddNumberToObject(vb, "cutoff_hz", config.virtual_bass_cutoff_hz);
        cJSON_AddNumberToObject(vb, "intensity_pct", config.virtual_bass_intensity_pct);
        cJSON_AddBoolToObject(vb, "bass_enhanced", config.virtual_bass_enhanced);

        // Silence Detector
        cJSON *sd = cJSON_AddObjectToObject(dsp, "silence_detector");
        cJSON_AddBoolToObject(sd, "enabled", config.silence_detector_enabled);

        // PreEQ (vollständiger State)
        cJSON *preeq = cJSON_AddObjectToObject(dsp, "preeq");
        cJSON_AddBoolToObject(preeq, "enabled", config.preeq.block_enabled != 0);
        cJSON_AddNumberToObject(preeq, "pregain_db",
                                config.preeq.pre_gain_raw / 256.0);
        cJSON *filters = cJSON_AddArrayToObject(preeq, "filters");
        for (int i = 0; i < 10; i++) {
            const mvs_preeq_filter_t *f = &config.preeq.filters[i];
            cJSON *item = cJSON_CreateObject();
            cJSON_AddBoolToObject(item, "enabled", f->enabled != 0);
            cJSON_AddNumberToObject(item, "type", f->type);
            cJSON_AddNumberToObject(item, "frequency_hz", f->frequency_hz);
            cJSON_AddNumberToObject(item, "q", f->q_raw / 1024.0);
            cJSON_AddNumberToObject(item, "gain_db", f->gain_raw / 256.0);
            cJSON_AddItemToArray(filters, item);
        }

        // DRC (vollständiger State)
        cJSON *drc = cJSON_AddObjectToObject(dsp, "drc");
        cJSON_AddBoolToObject(drc, "enabled", config.drc.enabled != 0);
        cJSON_AddNumberToObject(drc, "mode", config.drc.mode);
        cJSON *drc_bands = cJSON_AddArrayToObject(drc, "bands");
        for (int i = 0; i < 4; i++) {
            cJSON *band = cJSON_CreateObject();
            cJSON_AddNumberToObject(band, "index", i);
            cJSON_AddNumberToObject(band, "pregain_raw", config.drc.pregains[i]);
            cJSON_AddNumberToObject(band, "threshold_raw", config.drc.thresholds[i]);
            cJSON_AddNumberToObject(band, "ratio_raw", config.drc.ratios[i]);
            cJSON_AddNumberToObject(band, "attack_ms", config.drc.attacks[i]);
            cJSON_AddNumberToObject(band, "release_ms", config.drc.releases[i]);
            cJSON_AddItemToArray(drc_bands, band);
        }
    } else {
        cJSON_AddNullToObject(root, "dsp");
        ESP_LOGW(TAG, "Keine gespeicherte DSP-Konfiguration für Export");
    }

    *json = cJSON_Print(root);
    cJSON_Delete(root);

    if (!*json) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "DSP-Konfiguration exportiert (%zu Bytes)", strlen(*json));
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Import (Validierung + Parsing, kein Write)
// ---------------------------------------------------------------------------

static bool validate_preq(const cJSON *preeq)
{
    if (!cJSON_IsObject(preeq)) return false;
    if (!cJSON_IsBool(cJSON_GetObjectItem(preeq, "enabled"))) return false;
    cJSON *pregain = cJSON_GetObjectItem(preeq, "pregain_db");
    if (!cJSON_IsNumber(pregain) ||
        pregain->valuedouble < -128.0 || pregain->valuedouble > 127.996) return false;
    cJSON *filters = cJSON_GetObjectItem(preeq, "filters");
    if (!cJSON_IsArray(filters) || cJSON_GetArraySize(filters) != 10) return false;
    for (int i = 0; i < 10; i++) {
        cJSON *f = cJSON_GetArrayItem(filters, i);
        if (!cJSON_IsObject(f)) return false;
        if (!cJSON_IsBool(cJSON_GetObjectItem(f, "enabled"))) return false;
        cJSON *t = cJSON_GetObjectItem(f, "type");
        if (!cJSON_IsNumber(t) || t->valueint < 0 || t->valueint > 8) return false;
        cJSON *hz = cJSON_GetObjectItem(f, "frequency_hz");
        if (!cJSON_IsNumber(hz) || hz->valuedouble < 1 || hz->valuedouble > UINT16_MAX) return false;
        cJSON *q = cJSON_GetObjectItem(f, "q");
        if (!cJSON_IsNumber(q) || q->valuedouble <= 0 || q->valuedouble > 63.999) return false;
        cJSON *g = cJSON_GetObjectItem(f, "gain_db");
        if (!cJSON_IsNumber(g) || g->valuedouble < -128.0 || g->valuedouble > 127.996) return false;
    }
    return true;
}

static bool validate_drc(const cJSON *drc)
{
    if (!cJSON_IsObject(drc)) return false;
    if (!cJSON_IsBool(cJSON_GetObjectItem(drc, "enabled"))) return false;
    cJSON *mode = cJSON_GetObjectItem(drc, "mode");
    if (!cJSON_IsNumber(mode)) return false;
    cJSON *bands = cJSON_GetObjectItem(drc, "bands");
    if (!cJSON_IsArray(bands) || cJSON_GetArraySize(bands) != 4) return false;
    for (int i = 0; i < 4; i++) {
        cJSON *b = cJSON_GetArrayItem(bands, i);
        if (!cJSON_IsObject(b)) return false;
        if (!cJSON_IsNumber(cJSON_GetObjectItem(b, "pregain_raw")) ||
            !cJSON_IsNumber(cJSON_GetObjectItem(b, "threshold_raw")) ||
            !cJSON_IsNumber(cJSON_GetObjectItem(b, "ratio_raw")) ||
            !cJSON_IsNumber(cJSON_GetObjectItem(b, "attack_ms")) ||
            !cJSON_IsNumber(cJSON_GetObjectItem(b, "release_ms"))) return false;
    }
    return true;
}

esp_err_t config_io_parse_import(const char *json, dsp_profile_t *profile)
{
    if (!json || !profile) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "Import: Ungültiges JSON");
        return ESP_ERR_INVALID_ARG;
    }

    // Schema-Version prüfen
    cJSON *schema_ver = cJSON_GetObjectItem(root, "schema_version");
    if (!cJSON_IsNumber(schema_ver)) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Import: Keine schema_version");
        return ESP_ERR_INVALID_ARG;
    }
    if (schema_ver->valueint != DSP_CONFIG_SCHEMA_VERSION) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Import: Falsche schema_version %d (erwartet %d)",
                 schema_ver->valueint, DSP_CONFIG_SCHEMA_VERSION);
        return ESP_ERR_INVALID_ARG;
    }

    // Typ prüfen
    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "a800x-dsp-config") != 0) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Import: Falscher oder fehlender 'type'");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *dsp = cJSON_GetObjectItem(root, "dsp");
    if (!cJSON_IsObject(dsp)) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Import: Kein 'dsp'-Objekt");
        return ESP_ERR_INVALID_ARG;
    }

    dsp_model_get_default_profile(profile);

    // Noise Suppressor
    cJSON *ns = cJSON_GetObjectItem(dsp, "noise_suppressor");
    if (!cJSON_IsObject(ns)) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
    cJSON *item = cJSON_GetObjectItem(ns, "enabled");
    if (!cJSON_IsBool(item)) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
    profile->noise_suppressor_enabled = cJSON_IsTrue(item);
    item = cJSON_GetObjectItem(ns, "threshold_db");
    if (!cJSON_IsNumber(item)) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
    profile->noise_suppressor_threshold_raw = (int16_t)(item->valuedouble * 100.0);
    item = cJSON_GetObjectItem(ns, "ratio");
    profile->noise_suppressor_ratio = (uint16_t)item->valuedouble;
    item = cJSON_GetObjectItem(ns, "attack_ms");
    profile->noise_suppressor_attack_ms = (uint16_t)item->valuedouble;
    item = cJSON_GetObjectItem(ns, "release_ms");
    profile->noise_suppressor_release_ms = (uint16_t)item->valuedouble;

    // Virtual Bass
    cJSON *vb = cJSON_GetObjectItem(dsp, "virtual_bass");
    if (!cJSON_IsObject(vb)) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
    item = cJSON_GetObjectItem(vb, "enabled");
    profile->virtual_bass_enabled = cJSON_IsTrue(item);
    item = cJSON_GetObjectItem(vb, "cutoff_hz");
    profile->virtual_bass_cutoff_hz = (uint16_t)item->valuedouble;
    item = cJSON_GetObjectItem(vb, "intensity_pct");
    profile->virtual_bass_intensity_pct = (uint16_t)item->valuedouble;
    item = cJSON_GetObjectItem(vb, "bass_enhanced");
    profile->virtual_bass_enhanced = cJSON_IsTrue(item);

    // Silence Detector
    cJSON *sd = cJSON_GetObjectItem(dsp, "silence_detector");
    if (!cJSON_IsObject(sd)) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
    item = cJSON_GetObjectItem(sd, "enabled");
    profile->silence_detector_enabled = cJSON_IsTrue(item);

    // PreEQ
    cJSON *preeq = cJSON_GetObjectItem(dsp, "preeq");
    if (!validate_preq(preeq)) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
    profile->preeq.block_enabled = cJSON_IsTrue(cJSON_GetObjectItem(preeq, "enabled")) ? 1 : 0;
    profile->preeq.pre_gain_raw = (int16_t)(
        cJSON_GetObjectItem(preeq, "pregain_db")->valuedouble * 256.0);
    cJSON *filters = cJSON_GetObjectItem(preeq, "filters");
    for (int i = 0; i < 10; i++) {
        cJSON *f = cJSON_GetArrayItem(filters, i);
        profile->preeq.filters[i].enabled = cJSON_IsTrue(cJSON_GetObjectItem(f, "enabled")) ? 1 : 0;
        profile->preeq.filters[i].type = (uint8_t)cJSON_GetObjectItem(f, "type")->valueint;
        profile->preeq.filters[i].frequency_hz = (uint16_t)cJSON_GetObjectItem(f, "frequency_hz")->valuedouble;
        profile->preeq.filters[i].q_raw = (uint16_t)(cJSON_GetObjectItem(f, "q")->valuedouble * 1024.0);
        profile->preeq.filters[i].gain_raw = (int16_t)(cJSON_GetObjectItem(f, "gain_db")->valuedouble * 256.0);
    }

    // DRC
    cJSON *drc = cJSON_GetObjectItem(dsp, "drc");
    if (!validate_drc(drc)) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
    profile->drc.enabled = cJSON_IsTrue(cJSON_GetObjectItem(drc, "enabled")) ? 1 : 0;
    profile->drc.mode = (uint8_t)cJSON_GetObjectItem(drc, "mode")->valueint;
    cJSON *bands = cJSON_GetObjectItem(drc, "bands");
    for (int i = 0; i < 4; i++) {
        cJSON *b = cJSON_GetArrayItem(bands, i);
        profile->drc.pregains[i] = (uint16_t)cJSON_GetObjectItem(b, "pregain_raw")->valuedouble;
        profile->drc.thresholds[i] = (int16_t)cJSON_GetObjectItem(b, "threshold_raw")->valuedouble;
        profile->drc.ratios[i] = (uint16_t)cJSON_GetObjectItem(b, "ratio_raw")->valuedouble;
        profile->drc.attacks[i] = (uint16_t)cJSON_GetObjectItem(b, "attack_ms")->valuedouble;
        profile->drc.releases[i] = (uint16_t)cJSON_GetObjectItem(b, "release_ms")->valuedouble;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "DSP-Konfiguration validiert und geparst");
    return ESP_OK;
}
