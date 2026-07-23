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

static const char *TAG = "bp10_config";

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

esp_err_t config_io_export(char **json)
{
    if (!json) return ESP_ERR_INVALID_ARG;

    const mvs_device_profile_t *device = dsp_model_get_device_profile();
    if (!device->valid ||
        (device->kind != MVS_DEVICE_A800X_FIXED &&
         (device->kind != MVS_DEVICE_GENERIC_ACP ||
          !device->fingerprint_valid))) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    // Schema & Version
    cJSON_AddNumberToObject(root, "format_version", DSP_CONFIG_FORMAT_VERSION);
    cJSON_AddNumberToObject(root, "schema_version", DSP_CONFIG_SCHEMA_VERSION);
    cJSON_AddStringToObject(root, "app_version", APP_VERSION);
    cJSON_AddStringToObject(root, "type", "bp10-dsp-config");
    cJSON_AddStringToObject(root, "device_type",
        device->kind == MVS_DEVICE_A800X_FIXED ? "a800x" : "generic_acp");

    if (device->kind == MVS_DEVICE_GENERIC_ACP) {
        const mvs_schema_fingerprint_t *fp = &device->schema_fingerprint;
        cJSON *fingerprint = cJSON_AddObjectToObject(root, "schema_fingerprint");
        cJSON_AddNumberToObject(fingerprint, "vid", fp->vid);
        cJSON_AddNumberToObject(fingerprint, "pid", fp->pid);
        cJSON_AddNumberToObject(fingerprint, "adapter_kind", fp->adapter_kind);
        cJSON_AddNumberToObject(fingerprint, "module_type_count",
                                fp->module_type_count);
        cJSON *types = cJSON_AddArrayToObject(fingerprint, "module_types");
        for (uint8_t i = 0; i < fp->module_type_count; ++i) {
            cJSON_AddItemToArray(types,
                                 cJSON_CreateNumber(fp->module_types[i]));
        }
    }

    // DSP-Konfiguration aus NVS laden
    dsp_profile_t config;
    esp_err_t err = device->kind == MVS_DEVICE_A800X_FIXED
        ? nvs_settings_load_a800x_config(&config)
        : nvs_settings_load_generic_config(&device->schema_fingerprint, &config);
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

        if (device->virtual_bass_classic.available &&
            config.phase2_extended_valid) {
            cJSON *vbc = cJSON_AddObjectToObject(
                dsp, "virtual_bass_classic");
            cJSON_AddBoolToObject(
                vbc, "enabled", config.virtual_bass_classic_enabled);
            cJSON_AddNumberToObject(
                vbc, "cutoff_hz", config.virtual_bass_classic_cutoff_hz);
            cJSON_AddNumberToObject(
                vbc, "intensity_pct",
                config.virtual_bass_classic_intensity_pct);
        }

        if (device->phase.available && config.phase2_extended_valid) {
            cJSON *phase = cJSON_AddObjectToObject(dsp, "music_phase");
            cJSON_AddBoolToObject(phase, "inverted", config.phase_invert);
        }

        if (device->delay_hq.available && config.phase2_extended_valid) {
            cJSON *delay = cJSON_AddObjectToObject(dsp, "music_delay");
            cJSON_AddBoolToObject(delay, "enabled", config.delay_enabled);
            cJSON_AddNumberToObject(delay, "delay_ms", config.delay_ms);
            cJSON_AddBoolToObject(
                delay, "hq_enabled", config.delay_hq_enabled);
        }

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

static bool json_integer_in_range(const cJSON *item, int min, int max)
{
    return cJSON_IsNumber(item) && item->valuedouble == item->valueint &&
           item->valueint >= min && item->valueint <= max;
}

static bool parse_fingerprint(const cJSON *json,
                              mvs_schema_fingerprint_t *fingerprint)
{
    if (!cJSON_IsObject(json) || !fingerprint) return false;
    memset(fingerprint, 0, sizeof(*fingerprint));

    cJSON *vid = cJSON_GetObjectItem(json, "vid");
    cJSON *pid = cJSON_GetObjectItem(json, "pid");
    cJSON *adapter = cJSON_GetObjectItem(json, "adapter_kind");
    cJSON *count = cJSON_GetObjectItem(json, "module_type_count");
    cJSON *types = cJSON_GetObjectItem(json, "module_types");
    if (!json_integer_in_range(vid, 0, UINT16_MAX) ||
        !json_integer_in_range(pid, 0, UINT16_MAX) ||
        !json_integer_in_range(adapter, 0, UINT8_MAX) ||
        !json_integer_in_range(count, 0, MVS_FP_MAX_MODULE_TYPES) ||
        !cJSON_IsArray(types) || cJSON_GetArraySize(types) != count->valueint) {
        return false;
    }

    fingerprint->vid = (uint16_t)vid->valueint;
    fingerprint->pid = (uint16_t)pid->valueint;
    fingerprint->adapter_kind = (uint8_t)adapter->valueint;
    fingerprint->module_type_count = (uint8_t)count->valueint;
    for (uint8_t i = 0; i < fingerprint->module_type_count; ++i) {
        cJSON *type = cJSON_GetArrayItem(types, i);
        if (!json_integer_in_range(type, 0, UINT16_MAX)) return false;
        fingerprint->module_types[i] = (uint16_t)type->valueint;
    }
    return true;
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
        if (!cJSON_IsNumber(hz) || hz->valuedouble < 0 || hz->valuedouble > UINT16_MAX) return false;
        cJSON *q = cJSON_GetObjectItem(f, "q");
        if (!cJSON_IsNumber(q) || q->valuedouble < 0 || q->valuedouble > 63.999) return false;
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

    // format_version is the binding compatibility gate. Legacy files without
    // it are deliberately rejected instead of being guessed.
    cJSON *format_ver = cJSON_GetObjectItem(root, "format_version");
    if (!cJSON_IsNumber(format_ver) ||
        format_ver->valuedouble != format_ver->valueint ||
        format_ver->valueint != DSP_CONFIG_FORMAT_VERSION) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Import: Fehlende oder nicht unterstützte format_version");
        return ESP_ERR_NOT_SUPPORTED;
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
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "bp10-dsp-config") != 0) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Import: Falscher oder fehlender 'type'");
        return ESP_ERR_INVALID_ARG;
    }

    const mvs_device_profile_t *device = dsp_model_get_device_profile();
    cJSON *device_type = cJSON_GetObjectItem(root, "device_type");
    if (!device->valid || !cJSON_IsString(device_type)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_STATE;
    }
    if (device->kind == MVS_DEVICE_A800X_FIXED) {
        if (strcmp(device_type->valuestring, "a800x") != 0) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_STATE;
        }
    } else if (device->kind == MVS_DEVICE_GENERIC_ACP &&
               device->fingerprint_valid) {
        mvs_schema_fingerprint_t imported;
        if (strcmp(device_type->valuestring, "generic_acp") != 0 ||
            !parse_fingerprint(cJSON_GetObjectItem(root, "schema_fingerprint"),
                               &imported) ||
            !mvs_fingerprint_equal(&imported, &device->schema_fingerprint)) {
            cJSON_Delete(root);
            ESP_LOGE(TAG, "Import: Generic schema fingerprint mismatch");
            return ESP_ERR_INVALID_STATE;
        }
    } else {
        cJSON_Delete(root);
        return ESP_ERR_NOT_SUPPORTED;
    }

    cJSON *dsp = cJSON_GetObjectItem(root, "dsp");
    if (!cJSON_IsObject(dsp)) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Import: Kein 'dsp'-Objekt");
        return ESP_ERR_INVALID_ARG;
    }

    if (device->kind == MVS_DEVICE_A800X_FIXED) {
        if (!dsp_model_get_default_profile(profile)) {
            cJSON_Delete(root);
            return ESP_ERR_NOT_SUPPORTED;
        }
        // Schema v1 originally omitted the appended Phase-2 fields. Preserve
        // the confirmed runtime values unless the import explicitly carries
        // the complete extended section below.
        dsp_profile_t runtime;
        dsp_model_get_profile(&runtime);
        profile->virtual_bass_classic_enabled =
            runtime.virtual_bass_classic_enabled;
        profile->virtual_bass_classic_cutoff_hz =
            runtime.virtual_bass_classic_cutoff_hz;
        profile->virtual_bass_classic_intensity_pct =
            runtime.virtual_bass_classic_intensity_pct;
        profile->phase_invert = runtime.phase_invert;
        profile->delay_enabled = runtime.delay_enabled;
        profile->delay_ms = runtime.delay_ms;
        profile->delay_hq_enabled = runtime.delay_hq_enabled;
        profile->phase2_extended_valid = runtime.phase2_extended_valid;
    } else {
        // Preserve Generic-only fields that are not part of schema v1. Never
        // synthesize A800X factory values for a discovered device.
        dsp_model_get_profile(profile);
    }

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

    bool extended_complete = true;
    if (device->virtual_bass_classic.available) {
        cJSON *vbc = cJSON_GetObjectItem(dsp, "virtual_bass_classic");
        cJSON *enabled = cJSON_GetObjectItem(vbc, "enabled");
        cJSON *cutoff = cJSON_GetObjectItem(vbc, "cutoff_hz");
        cJSON *intensity = cJSON_GetObjectItem(vbc, "intensity_pct");
        if (!cJSON_IsObject(vbc) || !cJSON_IsBool(enabled) ||
            !json_integer_in_range(cutoff, 0, UINT16_MAX) ||
            !json_integer_in_range(intensity, 0, UINT16_MAX)) {
            extended_complete = false;
        } else {
            profile->virtual_bass_classic_enabled = cJSON_IsTrue(enabled);
            profile->virtual_bass_classic_cutoff_hz =
                (uint16_t)cutoff->valueint;
            profile->virtual_bass_classic_intensity_pct =
                (uint16_t)intensity->valueint;
        }
    }
    if (device->phase.available) {
        cJSON *phase = cJSON_GetObjectItem(dsp, "music_phase");
        cJSON *inverted = cJSON_GetObjectItem(phase, "inverted");
        if (!cJSON_IsObject(phase) || !cJSON_IsBool(inverted)) {
            extended_complete = false;
        } else {
            profile->phase_invert = cJSON_IsTrue(inverted);
        }
    }
    if (device->delay_hq.available) {
        cJSON *delay = cJSON_GetObjectItem(dsp, "music_delay");
        cJSON *enabled = cJSON_GetObjectItem(delay, "enabled");
        cJSON *delay_ms = cJSON_GetObjectItem(delay, "delay_ms");
        cJSON *hq_enabled = cJSON_GetObjectItem(delay, "hq_enabled");
        if (!cJSON_IsObject(delay) || !cJSON_IsBool(enabled) ||
            !json_integer_in_range(delay_ms, 0, UINT16_MAX) ||
            !cJSON_IsBool(hq_enabled)) {
            extended_complete = false;
        } else {
            profile->delay_enabled = cJSON_IsTrue(enabled);
            profile->delay_ms = (uint16_t)delay_ms->valueint;
            profile->delay_hq_enabled = cJSON_IsTrue(hq_enabled);
        }
    }
    if (device->virtual_bass_classic.available || device->phase.available ||
        device->delay_hq.available) {
        profile->phase2_extended_valid = extended_complete;
    }

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
