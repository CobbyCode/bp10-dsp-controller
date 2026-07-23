// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// config_io.c — DSP-Konfiguration Import/Export (JSON)
//
// v2: Multi-Path-Export (Music + REC getrennt), Schema v1-kompatibler Import

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
// Export (v2 Multi-Path)
// ---------------------------------------------------------------------------

static cJSON *export_profile(const dsp_profile_t *p,
                              const mvs_effect_path_t *path)
{
    cJSON *dsp = cJSON_CreateObject();

    if (path && path->noise_suppressor.available) {
        cJSON *ns = cJSON_AddObjectToObject(dsp, "noise_suppressor");
        cJSON_AddBoolToObject(ns, "enabled", p->noise_suppressor_enabled);
        cJSON_AddNumberToObject(ns, "threshold_db", p->noise_suppressor_threshold_raw / 100.0);
        cJSON_AddNumberToObject(ns, "ratio", p->noise_suppressor_ratio);
        cJSON_AddNumberToObject(ns, "attack_ms", p->noise_suppressor_attack_ms);
        cJSON_AddNumberToObject(ns, "release_ms", p->noise_suppressor_release_ms);
    }

    // Virtual Bass
    if (path && path->virtual_bass.available) {
        cJSON *vb = cJSON_AddObjectToObject(dsp, "virtual_bass");
        cJSON_AddBoolToObject(vb, "enabled", p->virtual_bass_enabled);
        cJSON_AddNumberToObject(vb, "cutoff_hz", p->virtual_bass_cutoff_hz);
        cJSON_AddNumberToObject(vb, "intensity_pct", p->virtual_bass_intensity_pct);
        cJSON_AddBoolToObject(vb, "bass_enhanced", p->virtual_bass_enhanced);
    }

    // Virtual Bass Classic (nur wenn Pfad-Capability vorhanden)
    if (path && path->virtual_bass_classic.available) {
        cJSON *vbc = cJSON_AddObjectToObject(dsp, "virtual_bass_classic");
        cJSON_AddBoolToObject(vbc, "enabled", p->virtual_bass_classic_enabled);
        cJSON_AddNumberToObject(vbc, "cutoff_hz", p->virtual_bass_classic_cutoff_hz);
        cJSON_AddNumberToObject(vbc, "intensity_pct", p->virtual_bass_classic_intensity_pct);
    }

    // Phase (nur wenn Pfad-Capability vorhanden)
    if (path && path->phase.available) {
        cJSON *phase = cJSON_AddObjectToObject(dsp, "music_phase");
        cJSON_AddBoolToObject(phase, "inverted", p->phase_invert);
    }

    // Delay (nur wenn Pfad-Capability vorhanden)
    if (path && path->delay_hq.available) {
        cJSON *delay = cJSON_AddObjectToObject(dsp, "music_delay");
        cJSON_AddBoolToObject(delay, "enabled", p->delay_enabled);
        cJSON_AddNumberToObject(delay, "delay_ms", p->delay_ms);
        cJSON_AddBoolToObject(delay, "hq_enabled", p->delay_hq_enabled);
    }

    // Silence Detector
    if (path && path->silence_detector.available) {
        cJSON *sd = cJSON_AddObjectToObject(dsp, "silence_detector");
        cJSON_AddBoolToObject(sd, "enabled", p->silence_detector_enabled);
    }

    // PreEQ
    if (path && path->preeq.available) {
        cJSON *preeq = cJSON_AddObjectToObject(dsp, "preeq");
        cJSON_AddBoolToObject(preeq, "enabled", p->preeq.block_enabled != 0);
        cJSON_AddNumberToObject(preeq, "pregain_db", p->preeq.pre_gain_raw / 256.0);
        cJSON *filters = cJSON_AddArrayToObject(preeq, "filters");
        for (int i = 0; i < 10; i++) {
            const mvs_preeq_filter_t *f = &p->preeq.filters[i];
            cJSON *item = cJSON_CreateObject();
            cJSON_AddBoolToObject(item, "enabled", f->enabled != 0);
            cJSON_AddNumberToObject(item, "type", f->type);
            cJSON_AddNumberToObject(item, "frequency_hz", f->frequency_hz);
            cJSON_AddNumberToObject(item, "q", f->q_raw / 1024.0);
            cJSON_AddNumberToObject(item, "gain_db", f->gain_raw / 256.0);
            cJSON_AddItemToArray(filters, item);
        }
    }

    // Out EQ (nur wenn Pfad-Capability vorhanden)
    if (path && path->out_eq.available) {
        cJSON *outeq = cJSON_AddObjectToObject(dsp, "out_eq");
        cJSON_AddBoolToObject(outeq, "enabled", p->out_eq.block_enabled != 0);
        cJSON_AddNumberToObject(outeq, "pregain_db", p->out_eq.pre_gain_raw / 256.0);
        cJSON *of = cJSON_AddArrayToObject(outeq, "filters");
        for (int i = 0; i < 10; i++) {
            const mvs_preeq_filter_t *f = &p->out_eq.filters[i];
            cJSON *item = cJSON_CreateObject();
            cJSON_AddBoolToObject(item, "enabled", f->enabled != 0);
            cJSON_AddNumberToObject(item, "type", f->type);
            cJSON_AddNumberToObject(item, "frequency_hz", f->frequency_hz);
            cJSON_AddNumberToObject(item, "q", f->q_raw / 1024.0);
            cJSON_AddNumberToObject(item, "gain_db", f->gain_raw / 256.0);
            cJSON_AddItemToArray(of, item);
        }
    }

    // USB Out Gain (nur wenn Pfad-Capability vorhanden)
    if (path && path->usb_out_gain.available) {
        cJSON *ug = cJSON_AddObjectToObject(dsp, "usb_out_gain");
        cJSON_AddNumberToObject(ug, "gain_raw", p->usb_out_gain);
    }

    // DRC
    if (path && path->drc.available) {
        cJSON *drc = cJSON_AddObjectToObject(dsp, "drc");
        cJSON_AddBoolToObject(drc, "enabled", p->drc.enabled != 0);
        cJSON_AddNumberToObject(drc, "mode", p->drc.mode);
        cJSON *bands = cJSON_AddArrayToObject(drc, "bands");
        for (int i = 0; i < 4; i++) {
            cJSON *band = cJSON_CreateObject();
            cJSON_AddNumberToObject(band, "index", i);
            cJSON_AddNumberToObject(band, "pregain_raw", p->drc.pregains[i]);
            cJSON_AddNumberToObject(band, "threshold_raw", p->drc.thresholds[i]);
            cJSON_AddNumberToObject(band, "ratio_raw", p->drc.ratios[i]);
            cJSON_AddNumberToObject(band, "attack_ms", p->drc.attacks[i]);
            cJSON_AddNumberToObject(band, "release_ms", p->drc.releases[i]);
            cJSON_AddItemToArray(bands, band);
        }
    }

    return dsp;
}

esp_err_t config_io_export(char **json)
{
    if (!json) return ESP_ERR_INVALID_ARG;

    const mvs_device_profile_t *device = dsp_model_get_device_profile();
    if (!device->valid ||
        (device->kind != MVS_DEVICE_A800X_FIXED &&
         (device->kind != MVS_DEVICE_GENERIC_ACP || !device->fingerprint_valid))) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON_AddNumberToObject(root, "format_version", DSP_CONFIG_FORMAT_VERSION);
    // A800X und Single-Path: schema_version 1, Multi-Path: 2
    int schema_ver = (device->kind == MVS_DEVICE_GENERIC_ACP && device->path_count > 1) ? 2 : 1;
    cJSON_AddNumberToObject(root, "schema_version", schema_ver);
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
        cJSON_AddNumberToObject(fingerprint, "module_type_count", fp->module_type_count);
        cJSON *types = cJSON_AddArrayToObject(fingerprint, "module_types");
        for (uint8_t i = 0; i < fp->module_type_count; ++i)
            cJSON_AddItemToArray(types, cJSON_CreateNumber(fp->module_types[i]));
    }

    // Multi-Path-Export
    if (device->kind == MVS_DEVICE_GENERIC_ACP && device->path_count > 1) {
        const mvs_schema_fingerprint_t *fp = &device->schema_fingerprint;
        dsp_multi_config_t config;
        esp_err_t err = nvs_settings_load_generic_config(fp, &config);
        if (err == ESP_OK) {
            cJSON *paths = cJSON_AddObjectToObject(root, "paths");
            cJSON_AddItemToObject(paths, "music", export_profile(&config.music,
                mvs_device_profile_get_path(device, MVS_PATH_MUSIC)));
            if (config.rec_valid)
                cJSON_AddItemToObject(paths, "rec", export_profile(&config.rec,
                    mvs_device_profile_get_path(device, MVS_PATH_REC)));
        } else {
            // Fallback: aktuelles Runtime-Profil lesen
            cJSON *paths = cJSON_AddObjectToObject(root, "paths");
            dsp_multi_config_t current;
            dsp_model_get_multi_config(&current);
            cJSON_AddItemToObject(paths, "music", export_profile(&current.music,
                mvs_device_profile_get_path(device, MVS_PATH_MUSIC)));
            if (current.rec_valid)
                cJSON_AddItemToObject(paths, "rec", export_profile(&current.rec,
                    mvs_device_profile_get_path(device, MVS_PATH_REC)));
        }
    } else {
        // A800X oder Single-Path Generic: Legacy-Format
        if (device->kind == MVS_DEVICE_A800X_FIXED) {
            dsp_profile_t config;
            if (nvs_settings_load_a800x_config(&config) == ESP_OK) {
                cJSON_AddItemToObject(root, "dsp", export_profile(&config,
                    &device->paths[MVS_PATH_MUSIC]));
            } else {
                cJSON_AddNullToObject(root, "dsp");
            }
        } else {
            dsp_profile_t p;
            dsp_model_get_profile(&p);
            cJSON_AddItemToObject(root, "dsp", export_profile(&p,
                mvs_device_profile_get_path(device, MVS_PATH_MUSIC)));
        }
    }

    *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!*json) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "DSP-Konfiguration exportiert (v2, %zu Bytes)", strlen(*json));
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Import helpers
// ---------------------------------------------------------------------------

static bool json_integer_in_range(const cJSON *item, int min, int max)
{
    return cJSON_IsNumber(item) && item->valuedouble == item->valueint &&
           item->valueint >= min && item->valueint <= max;
}

static bool parse_fingerprint(const cJSON *json, mvs_schema_fingerprint_t *fp)
{
    if (!cJSON_IsObject(json) || !fp) return false;
    memset(fp, 0, sizeof(*fp));
    cJSON *vid = cJSON_GetObjectItem(json, "vid");
    cJSON *pid = cJSON_GetObjectItem(json, "pid");
    cJSON *adapter = cJSON_GetObjectItem(json, "adapter_kind");
    cJSON *count = cJSON_GetObjectItem(json, "module_type_count");
    cJSON *types = cJSON_GetObjectItem(json, "module_types");
    if (!json_integer_in_range(vid, 0, UINT16_MAX) ||
        !json_integer_in_range(pid, 0, UINT16_MAX) ||
        !json_integer_in_range(adapter, 0, UINT8_MAX) ||
        !json_integer_in_range(count, 0, MVS_FP_MAX_MODULE_TYPES) ||
        !cJSON_IsArray(types) || cJSON_GetArraySize(types) != count->valueint)
        return false;
    fp->vid = (uint16_t)vid->valueint;
    fp->pid = (uint16_t)pid->valueint;
    fp->adapter_kind = (uint8_t)adapter->valueint;
    fp->module_type_count = (uint8_t)count->valueint;
    for (uint8_t i = 0; i < fp->module_type_count; ++i) {
        cJSON *type = cJSON_GetArrayItem(types, i);
        if (!json_integer_in_range(type, 0, UINT16_MAX)) return false;
        fp->module_types[i] = (uint16_t)type->valueint;
    }
    return true;
}

static bool validate_preq(const cJSON *preeq)
{
    if (!cJSON_IsObject(preeq)) return false;
    if (!cJSON_IsBool(cJSON_GetObjectItem(preeq, "enabled"))) return false;
    cJSON *pregain = cJSON_GetObjectItem(preeq, "pregain_db");
    if (!cJSON_IsNumber(pregain) || pregain->valuedouble < -128.0 || pregain->valuedouble > 127.996) return false;
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

static bool parse_profile(const cJSON *dsp, dsp_profile_t *profile,
                           const mvs_effect_path_t *path)
{
    if (!cJSON_IsObject(dsp) || !profile || !path) return false;

    // Noise Suppressor
    if (path->noise_suppressor.available) {
        cJSON *ns = cJSON_GetObjectItem(dsp, "noise_suppressor");
        cJSON *enabled = cJSON_GetObjectItem(ns, "enabled");
        cJSON *threshold = cJSON_GetObjectItem(ns, "threshold_db");
        cJSON *ratio = cJSON_GetObjectItem(ns, "ratio");
        cJSON *attack = cJSON_GetObjectItem(ns, "attack_ms");
        cJSON *release = cJSON_GetObjectItem(ns, "release_ms");
        if (!cJSON_IsObject(ns) || !cJSON_IsBool(enabled) ||
            !cJSON_IsNumber(threshold) || !cJSON_IsNumber(ratio) ||
            !cJSON_IsNumber(attack) || !cJSON_IsNumber(release))
            return false;
        profile->noise_suppressor_enabled = cJSON_IsTrue(enabled);
        profile->noise_suppressor_threshold_raw =
            (int16_t)(threshold->valuedouble * 100.0);
        profile->noise_suppressor_ratio = (uint16_t)ratio->valuedouble;
        profile->noise_suppressor_attack_ms = (uint16_t)attack->valuedouble;
        profile->noise_suppressor_release_ms = (uint16_t)release->valuedouble;
    }

    // Virtual Bass
    if (path->virtual_bass.available) {
        cJSON *vb = cJSON_GetObjectItem(dsp, "virtual_bass");
        cJSON *enabled = cJSON_GetObjectItem(vb, "enabled");
        cJSON *cutoff = cJSON_GetObjectItem(vb, "cutoff_hz");
        cJSON *intensity = cJSON_GetObjectItem(vb, "intensity_pct");
        cJSON *enhanced = cJSON_GetObjectItem(vb, "bass_enhanced");
        if (!cJSON_IsObject(vb) || !cJSON_IsBool(enabled) ||
            !cJSON_IsNumber(cutoff) || !cJSON_IsNumber(intensity) ||
            !cJSON_IsBool(enhanced))
            return false;
        profile->virtual_bass_enabled = cJSON_IsTrue(enabled);
        profile->virtual_bass_cutoff_hz = (uint16_t)cutoff->valuedouble;
        profile->virtual_bass_intensity_pct =
            (uint16_t)intensity->valuedouble;
        profile->virtual_bass_enhanced = cJSON_IsTrue(enhanced);
    }

    // VB Classic etc.
    bool extended = true;
    cJSON *vbc = cJSON_GetObjectItem(dsp, "virtual_bass_classic");
    if (cJSON_IsObject(vbc)) {
        profile->virtual_bass_classic_enabled = cJSON_IsTrue(cJSON_GetObjectItem(vbc, "enabled"));
        profile->virtual_bass_classic_cutoff_hz = (uint16_t)cJSON_GetObjectItem(vbc, "cutoff_hz")->valuedouble;
        profile->virtual_bass_classic_intensity_pct = (uint16_t)cJSON_GetObjectItem(vbc, "intensity_pct")->valuedouble;
    }
    cJSON *phase = cJSON_GetObjectItem(dsp, "music_phase");
    if (cJSON_IsObject(phase))
        profile->phase_invert = cJSON_IsTrue(cJSON_GetObjectItem(phase, "inverted"));
    cJSON *delay = cJSON_GetObjectItem(dsp, "music_delay");
    if (cJSON_IsObject(delay)) {
        profile->delay_enabled = cJSON_IsTrue(cJSON_GetObjectItem(delay, "enabled"));
        profile->delay_ms = (uint16_t)cJSON_GetObjectItem(delay, "delay_ms")->valuedouble;
        profile->delay_hq_enabled = cJSON_IsTrue(cJSON_GetObjectItem(delay, "hq_enabled"));
    }
    profile->phase2_extended_valid = extended;

    // Silence
    cJSON *sd = cJSON_GetObjectItem(dsp, "silence_detector");
    if (cJSON_IsObject(sd))
        profile->silence_detector_enabled = cJSON_IsTrue(cJSON_GetObjectItem(sd, "enabled"));

    // PreEQ
    if (path->preeq.available) {
        cJSON *preeq = cJSON_GetObjectItem(dsp, "preeq");
        if (!validate_preq(preeq)) return false;
        profile->preeq.block_enabled = cJSON_IsTrue(cJSON_GetObjectItem(preeq, "enabled")) ? 1 : 0;
        profile->preeq.pre_gain_raw = (int16_t)(cJSON_GetObjectItem(preeq, "pregain_db")->valuedouble * 256.0);
        cJSON *filters = cJSON_GetObjectItem(preeq, "filters");
        for (int i = 0; i < 10; i++) {
            cJSON *f = cJSON_GetArrayItem(filters, i);
            profile->preeq.filters[i].enabled = cJSON_IsTrue(cJSON_GetObjectItem(f, "enabled")) ? 1 : 0;
            profile->preeq.filters[i].type = (uint8_t)cJSON_GetObjectItem(f, "type")->valueint;
            profile->preeq.filters[i].frequency_hz = (uint16_t)cJSON_GetObjectItem(f, "frequency_hz")->valuedouble;
            profile->preeq.filters[i].q_raw = (uint16_t)(cJSON_GetObjectItem(f, "q")->valuedouble * 1024.0);
            profile->preeq.filters[i].gain_raw = (int16_t)(cJSON_GetObjectItem(f, "gain_db")->valuedouble * 256.0);
        }
    }

    // Out EQ
    cJSON *outeq = cJSON_GetObjectItem(dsp, "out_eq");
    if (cJSON_IsObject(outeq) && validate_preq(outeq)) {
        profile->out_eq_valid = true;
        profile->out_eq.block_enabled = cJSON_IsTrue(cJSON_GetObjectItem(outeq, "enabled")) ? 1 : 0;
        profile->out_eq.pre_gain_raw = (int16_t)(cJSON_GetObjectItem(outeq, "pregain_db")->valuedouble * 256.0);
        cJSON *of = cJSON_GetObjectItem(outeq, "filters");
        for (int i = 0; i < 10; i++) {
            cJSON *f = cJSON_GetArrayItem(of, i);
            profile->out_eq.filters[i].enabled = cJSON_IsTrue(cJSON_GetObjectItem(f, "enabled")) ? 1 : 0;
            profile->out_eq.filters[i].type = (uint8_t)cJSON_GetObjectItem(f, "type")->valueint;
            profile->out_eq.filters[i].frequency_hz = (uint16_t)cJSON_GetObjectItem(f, "frequency_hz")->valuedouble;
            profile->out_eq.filters[i].q_raw = (uint16_t)(cJSON_GetObjectItem(f, "q")->valuedouble * 1024.0);
            profile->out_eq.filters[i].gain_raw = (int16_t)(cJSON_GetObjectItem(f, "gain_db")->valuedouble * 256.0);
        }
    }

    // USB Out Gain
    cJSON *ug = cJSON_GetObjectItem(dsp, "usb_out_gain");
    if (cJSON_IsObject(ug))
        profile->usb_out_gain = (uint16_t)cJSON_GetObjectItem(ug, "gain_raw")->valuedouble;

    // DRC
    if (path->drc.available) {
        cJSON *drc = cJSON_GetObjectItem(dsp, "drc");
        if (!validate_drc(drc)) return false;
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
    }

    return true;
}

// ---------------------------------------------------------------------------
// Legacy Import (Single-Path → Music)
// ---------------------------------------------------------------------------

esp_err_t config_io_parse_import(const char *json, dsp_profile_t *profile)
{
    dsp_multi_config_t config;
    esp_err_t err = config_io_parse_import_multi(json, &config);
    if (err != ESP_OK) return err;
    memcpy(profile, &config.music, sizeof(dsp_profile_t));
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Multi-Path Import
// ---------------------------------------------------------------------------

esp_err_t config_io_parse_import_multi(const char *json,
                                        dsp_multi_config_t *config)
{
    if (!json || !config) return ESP_ERR_INVALID_ARG;
    memset(config, 0, sizeof(*config));

    cJSON *root = cJSON_Parse(json);
    if (!root) { ESP_LOGE(TAG, "Import: Invalid JSON"); return ESP_ERR_INVALID_ARG; }

    cJSON *format_ver = cJSON_GetObjectItem(root, "format_version");
    if (!cJSON_IsNumber(format_ver) || format_ver->valueint != DSP_CONFIG_FORMAT_VERSION) {
        cJSON_Delete(root); return ESP_ERR_NOT_SUPPORTED;
    }

    cJSON *schema_ver = cJSON_GetObjectItem(root, "schema_version");
    if (!cJSON_IsNumber(schema_ver) || (schema_ver->valueint != 1 && schema_ver->valueint != 2)) {
        cJSON_Delete(root); return ESP_ERR_INVALID_ARG;
    }
    int schema = schema_ver->valueint;

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "bp10-dsp-config") != 0) {
        cJSON_Delete(root); return ESP_ERR_INVALID_ARG;
    }

    const mvs_device_profile_t *device = dsp_model_get_device_profile();
    cJSON *device_type = cJSON_GetObjectItem(root, "device_type");
    if (!device->valid || !cJSON_IsString(device_type)) { cJSON_Delete(root); return ESP_ERR_INVALID_STATE; }

    if (device->kind == MVS_DEVICE_A800X_FIXED) {
        if (strcmp(device_type->valuestring, "a800x") != 0) { cJSON_Delete(root); return ESP_ERR_INVALID_STATE; }
    } else if (device->kind == MVS_DEVICE_GENERIC_ACP && device->fingerprint_valid) {
        mvs_schema_fingerprint_t imported;
        if (strcmp(device_type->valuestring, "generic_acp") != 0 ||
            !parse_fingerprint(cJSON_GetObjectItem(root, "schema_fingerprint"), &imported) ||
            !mvs_fingerprint_equal(&imported, &device->schema_fingerprint)) {
            cJSON_Delete(root); return ESP_ERR_INVALID_STATE;
        }
    } else { cJSON_Delete(root); return ESP_ERR_NOT_SUPPORTED; }

    if (schema >= 2) {
        // Multi-Path (v2)
        cJSON *paths = cJSON_GetObjectItem(root, "paths");
        if (!cJSON_IsObject(paths)) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }

        cJSON *music = cJSON_GetObjectItem(paths, "music");
        if (!parse_profile(music, &config->music, mvs_device_profile_get_path(device, MVS_PATH_MUSIC))) {
            cJSON_Delete(root); return ESP_ERR_INVALID_ARG;
        }

        cJSON *rec = cJSON_GetObjectItem(paths, "rec");
        if (cJSON_IsObject(rec) && device->paths[MVS_PATH_REC].present) {
            if (!parse_profile(rec, &config->rec, mvs_device_profile_get_path(device, MVS_PATH_REC))) {
                cJSON_Delete(root); return ESP_ERR_INVALID_ARG;
            }
            config->rec_valid = true;
        }

        config->schema_version = 2;
    } else {
        // Legacy Single-Path (v1) → als Music importieren
        // Runtime-Profil als Basis (wie 0.4.6): Nur im JSON vorhandene Felder werden
        // überschrieben, alles andere behält die aktuellen Laufzeitwerte.
        dsp_model_get_profile(&config->music);
        cJSON *dsp = cJSON_GetObjectItem(root, "dsp");
        if (!parse_profile(dsp, &config->music, mvs_device_profile_get_path(device, MVS_PATH_MUSIC))) {
            cJSON_Delete(root); return ESP_ERR_INVALID_ARG;
        }
        config->schema_version = 1;
        config->rec_valid = false;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "DSP-Konfiguration validiert (schema v%d)", schema);
    return ESP_OK;
}
