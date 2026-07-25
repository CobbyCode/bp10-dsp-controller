// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// dsp_model.c — DSP-Modell — Zustand und Parameter
//
// v2: Multi-Path-Unterstützung (Music / REC)

#include "dsp_model.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "usb_host_ctrl.h"
#include "mvs_protocol.h"
#include "mvs_device_profile.h"

static const char *TAG = "bp10_dsp_model";

// Geräteprofil (vom Discovery gesetzt)
static mvs_device_profile_t s_device_profile = {0};

// Aktuelle DSP-Profile (pro Pfad)
static dsp_profile_t s_music_profile = {0};
static dsp_profile_t s_rec_profile = {0};

// Legacy: aktuelles DSP-Profil (alias auf s_music_profile)
static dsp_profile_t s_current_profile = {0};
static char s_verify_mismatch[192] = {0};

static esp_err_t verify_mismatch_i64(mvs_path_id_t path_id,
    const char *module, uint8_t effect_id, const char *field,
    long long expected, long long actual)
{
    snprintf(s_verify_mismatch, sizeof(s_verify_mismatch),
             "%s | %s | %lld | %lld | mismatch",
             module, field, expected, actual);
    ESP_LOGE(TAG, "Verify mismatch path=%s effect=0x%02X: %s",
             path_id == MVS_PATH_REC ? "rec" : "music",
             effect_id, s_verify_mismatch);
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t verify_error(mvs_path_id_t path_id, const char *module,
                              uint8_t effect_id, const char *field,
                              const char *expected, const char *actual,
                              esp_err_t error)
{
    snprintf(s_verify_mismatch, sizeof(s_verify_mismatch),
             "%s | %s | %s | %s | %s",
             module, field, expected, actual, esp_err_to_name(error));
    ESP_LOGE(TAG, "Verify error path=%s effect=0x%02X: %s",
             path_id == MVS_PATH_REC ? "rec" : "music",
             effect_id, s_verify_mismatch);
    return error;
}

const char *dsp_model_get_verify_mismatch(void)
{
    return s_verify_mismatch;
}

// ---------------------------------------------------------------------------
// Forward declarations für Funktionen, die vor ihrer Definition verwendet werden
static esp_err_t read_drc_state_id(uint8_t effect_id, mvs_drc_state_t *state);
static esp_err_t read_drc_a800x_state_id(
    uint8_t effect_id, mvs_drc_packed_state_t *state);
static inline uint16_t read_u16_le(const uint8_t *buf);
static void load_drc_view(const dsp_profile_t *profile, dsp_drc_view_t *view);
static void store_drc_view(dsp_profile_t *profile, const dsp_drc_view_t *view);

static void log_full_read(const char *module, uint8_t expected_effect_id,
                          uint8_t expected_wire_len,
                          const uint8_t *report, uint16_t report_len)
{
    uint8_t actual_effect_id = report_len > 2 ? report[2] : 0;
    uint8_t actual_wire_len = report_len > 3 ? report[3] : 0;
    unsigned expected_frame_len = (unsigned)expected_wire_len + 5U;
    unsigned actual_frame_len = report_len > 3
        ? (unsigned)actual_wire_len + 5U : 0U;
    ESP_LOGI(TAG,
             "%s Full-Read Soll: effect=0x%02X wire=%u frame=%u; "
             "Ist: effect=0x%02X wire=%u frame=%u HID-report=%u",
             module, expected_effect_id, expected_wire_len,
             expected_frame_len, actual_effect_id, actual_wire_len,
             actual_frame_len, report_len);
    if (report_len > 0) {
        uint16_t raw_len = actual_frame_len > 0 &&
                           actual_frame_len <= report_len
            ? (uint16_t)actual_frame_len : report_len;
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, report, raw_len, ESP_LOG_INFO);
    }
}

// HID-Transfer-Helper
// ---------------------------------------------------------------------------

static esp_err_t send_mvs_command(const uint8_t *frame, uint16_t frame_len)
{
    uint8_t report[256];
    mvs_prepare_hid_report(frame, frame_len, report);
    return usb_host_ctrl_send_report(report, sizeof(report));
}

// ---------------------------------------------------------------------------
// Profil-Setter
// ---------------------------------------------------------------------------

void dsp_model_set_device_profile(const mvs_device_profile_t *profile)
{
    s_device_profile = profile ? *profile : (mvs_device_profile_t){0};
    if (!profile) return;
    const char *kind_name = "unknown";
    switch (profile->kind) {
        case MVS_DEVICE_A800X_FIXED: kind_name = "A800X"; break;
        case MVS_DEVICE_GENERIC_ACP: kind_name = "Generic ACP"; break;
        default: break;
    }
    ESP_LOGI(TAG, "Geräteprofil gesetzt: %s paths=%u", kind_name, profile->path_count);
}

const mvs_device_profile_t *dsp_model_get_device_profile(void)
{
    return &s_device_profile;
}

// ---------------------------------------------------------------------------
// Legacy Effekt-ID-Zugriff (Music-Pfad)
// ---------------------------------------------------------------------------

uint8_t dsp_model_get_effect_id_ns(void)    { return mvs_effect_id_ns(&s_device_profile); }
uint8_t dsp_model_get_effect_id_vb(void)    { return mvs_effect_id_vb(&s_device_profile); }
uint8_t dsp_model_get_effect_id_sd(void)    { return mvs_effect_id_sd(&s_device_profile); }
uint8_t dsp_model_get_effect_id_preeq(void) { return mvs_effect_id_preeq(&s_device_profile); }
uint8_t dsp_model_get_effect_id_drc(void)   { return mvs_effect_id_drc(&s_device_profile); }
uint8_t dsp_model_get_effect_id_vb_classic(void) { return mvs_effect_id_vb_classic(&s_device_profile); }
uint8_t dsp_model_get_effect_id_phase(void)       { return mvs_effect_id_phase(&s_device_profile); }
uint8_t dsp_model_get_effect_id_delay_hq(void)    { return mvs_effect_id_delay_hq(&s_device_profile); }
uint8_t dsp_model_get_effect_id_usb_out_gain(void) { return mvs_effect_id_usb_out_gain(&s_device_profile); }

// ---------------------------------------------------------------------------
// Pfad-basierte Effekt-ID
// ---------------------------------------------------------------------------

uint8_t dsp_model_get_path_effect_id(mvs_path_id_t path_id,
                                      mvs_module_kind_t module)
{
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    return mvs_path_effect_id(path, module);
}

// ---------------------------------------------------------------------------
// Profil-Helper
// ---------------------------------------------------------------------------

void dsp_model_get_profile(dsp_profile_t *out)
{
    if (out) memcpy(out, &s_current_profile, sizeof(dsp_profile_t));
}

void dsp_model_commit_profile(const dsp_profile_t *profile)
{
    if (profile) {
        memcpy(&s_current_profile, profile, sizeof(dsp_profile_t));
        memcpy(&s_music_profile, profile, sizeof(dsp_profile_t));
    }
}

void dsp_model_get_multi_config(dsp_multi_config_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->schema_version = 2;
    memcpy(&out->music, &s_music_profile, sizeof(dsp_profile_t));
    if (s_device_profile.paths[MVS_PATH_REC].present) {
        memcpy(&out->rec, &s_rec_profile, sizeof(dsp_profile_t));
        out->rec_valid = true;
    }
}

void dsp_model_commit_multi_config(const dsp_multi_config_t *config)
{
    if (!config) return;
    memcpy(&s_music_profile, &config->music, sizeof(dsp_profile_t));
    memcpy(&s_current_profile, &config->music, sizeof(dsp_profile_t));
    if (config->rec_valid) {
        memcpy(&s_rec_profile, &config->rec, sizeof(dsp_profile_t));
    }
}

void dsp_model_commit_path_profile(mvs_path_id_t path_id,
                                    const dsp_profile_t *profile)
{
    if (!profile) return;
    if (path_id == MVS_PATH_MUSIC) {
        memcpy(&s_music_profile, profile, sizeof(dsp_profile_t));
        memcpy(&s_current_profile, profile, sizeof(dsp_profile_t));
    } else if (path_id == MVS_PATH_REC) {
        memcpy(&s_rec_profile, profile, sizeof(dsp_profile_t));
    }
}

// ---------------------------------------------------------------------------
// Initialisierung
// ---------------------------------------------------------------------------

esp_err_t dsp_model_init(void)
{
    memset(&s_current_profile, 0, sizeof(s_current_profile));
    memset(&s_music_profile, 0, sizeof(s_music_profile));
    memset(&s_rec_profile, 0, sizeof(s_rec_profile));
    memset(&s_device_profile, 0, sizeof(s_device_profile));
    return ESP_OK;
}

bool dsp_model_get_default_profile(dsp_profile_t *profile)
{
    if (!profile) return false;
    memset(profile, 0, sizeof(dsp_profile_t));

    if (!s_device_profile.valid ||
        s_device_profile.kind != MVS_DEVICE_A800X_FIXED) {
        ESP_LOGW(TAG, "Keine Factory-Defaults für Geräteprofil %d",
                 s_device_profile.kind);
        return false;
    }

    profile->noise_suppressor_enabled = true;
    profile->noise_suppressor_threshold_raw = -5500;
    profile->noise_suppressor_ratio = 4;
    profile->noise_suppressor_attack_ms = 2;
    profile->noise_suppressor_release_ms = 100;
    profile->silence_detector_enabled = true;
    profile->virtual_bass_enabled = true;
    profile->virtual_bass_cutoff_hz = 42;
    profile->virtual_bass_intensity_pct = 4;
    profile->virtual_bass_enhanced = true;

    profile->preeq.block_enabled = true;
    profile->preeq.pre_gain_raw = 0;
    profile->preeq.selected_filter = 0;
    profile->preeq.filters[0].enabled = 1; profile->preeq.filters[0].type = MVS_FILTER_LP;
    profile->preeq.filters[0].frequency_hz = 280; profile->preeq.filters[0].q_raw = 1229;
    profile->preeq.filters[0].gain_raw = 0;
    profile->preeq.filters[1].enabled = 1; profile->preeq.filters[1].type = MVS_FILTER_LP;
    profile->preeq.filters[1].frequency_hz = 500; profile->preeq.filters[1].q_raw = 724;
    profile->preeq.filters[1].gain_raw = 0;
    profile->preeq.filters[2].enabled = 1; profile->preeq.filters[2].type = MVS_FILTER_HP;
    profile->preeq.filters[2].frequency_hz = 35; profile->preeq.filters[2].q_raw = 819;
    profile->preeq.filters[2].gain_raw = 0;
    profile->preeq.filters[3].enabled = 1; profile->preeq.filters[3].type = MVS_FILTER_PK;
    profile->preeq.filters[3].frequency_hz = 55; profile->preeq.filters[3].q_raw = 3584;
    profile->preeq.filters[3].gain_raw = 384;
    profile->preeq.filters[4].enabled = 1; profile->preeq.filters[4].type = MVS_FILTER_PK;
    profile->preeq.filters[4].frequency_hz = 85; profile->preeq.filters[4].q_raw = 3584;
    profile->preeq.filters[4].gain_raw = 384;
    for (int i = 5; i < 10; i++) {
        profile->preeq.filters[i].enabled = 0; profile->preeq.filters[i].type = MVS_FILTER_PK;
        profile->preeq.filters[i].frequency_hz = 20000; profile->preeq.filters[i].q_raw = 724;
        profile->preeq.filters[i].gain_raw = 0;
    }
    profile->drc.enabled = true; profile->drc.mode = 0;
    profile->drc.crossover_type = 1; profile->drc.crossover_q1_raw = 724;
    profile->drc.crossover_q2_raw = 724; profile->drc.crossover_freq1_hz = 300;
    profile->drc.crossover_freq2_hz = 2000;
    for (int i = 0; i < 3; i++) {
        profile->drc.thresholds[i] = 0; profile->drc.ratios[i] = 100;
        profile->drc.attacks[i] = 2; profile->drc.releases[i] = 100;
        profile->drc.pregains[i] = 4096;
    }
    profile->drc.thresholds[3] = -500; profile->drc.ratios[3] = 100;
    profile->drc.attacks[3] = 2; profile->drc.releases[3] = 800;
    profile->drc.pregains[3] = 5157;
    return true;
}

// ---------------------------------------------------------------------------
// dsp_model_apply_profile — vollständige Parametrierung (Legacy Music)
// ---------------------------------------------------------------------------

esp_err_t dsp_model_apply_profile(const dsp_profile_t *profile)
{
    return dsp_model_apply_path_profile(MVS_PATH_MUSIC, profile);
}

esp_err_t dsp_model_apply_multi_config(const dsp_multi_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    s_verify_mismatch[0] = '\0';
    esp_err_t err = dsp_model_apply_path_profile(MVS_PATH_MUSIC, &config->music);
    if (err != ESP_OK) return err;
    if (config->rec_valid && s_device_profile.paths[MVS_PATH_REC].present) {
        err = dsp_model_apply_path_profile(MVS_PATH_REC, &config->rec);
    }
    return err;
}

esp_err_t dsp_model_apply_path_profile(mvs_path_id_t path_id,
                                        const dsp_profile_t *profile)
{
    if (!profile) return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.valid) {
        ESP_LOGW(TAG, "Kein Geräteprofil gesetzt");
        return ESP_ERR_INVALID_STATE;
    }

    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path) {
        ESP_LOGW(TAG, "Pfad %d nicht verfügbar", path_id);
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "Wende DSP-Profil an (Pfad: %s)...", path->label);

    esp_err_t err, first_err = ESP_OK;

    // Noise Suppressor
    if (path->noise_suppressor.available) {
        err = dsp_model_set_noise_suppressor_state(
            profile->noise_suppressor_enabled, profile->noise_suppressor_threshold_raw,
            profile->noise_suppressor_ratio, profile->noise_suppressor_attack_ms,
            profile->noise_suppressor_release_ms);
        if (err != ESP_OK && first_err == ESP_OK) first_err = err;
    }

    // Virtual Bass (path-aware)
    if (path->virtual_bass.available) {
        err = dsp_model_set_virtual_bass_path(path_id,
            profile->virtual_bass_enabled, profile->virtual_bass_cutoff_hz,
            profile->virtual_bass_intensity_pct, profile->virtual_bass_enhanced);
        if (err != ESP_OK && first_err == ESP_OK) first_err = err;
    }

    // Silence Detector (nur Music-Pfad)
    if (path_id == MVS_PATH_MUSIC && s_device_profile.silence_detector.available) {
        err = dsp_model_set_silence_detector(profile->silence_detector_enabled);
        if (err != ESP_OK && first_err == ESP_OK) first_err = err;
    }

    // VB Classic (path-aware)
    if (profile->phase2_extended_valid && path->virtual_bass_classic.available) {
        err = dsp_model_set_virtual_bass_classic_path(path_id,
            profile->virtual_bass_classic_enabled,
            profile->virtual_bass_classic_cutoff_hz,
            profile->virtual_bass_classic_intensity_pct);
        if (err != ESP_OK && first_err == ESP_OK) first_err = err;
    }
    if (profile->phase2_extended_valid && path->phase.available) {
        err = dsp_model_set_phase(profile->phase_invert);
        if (err != ESP_OK && first_err == ESP_OK) first_err = err;
    }
    if (profile->phase2_extended_valid && path->delay_hq.available) {
        err = dsp_model_set_delay_path(path_id, profile->delay_enabled,
            profile->delay_ms, profile->delay_hq_enabled);
        if (err != ESP_OK && first_err == ESP_OK) first_err = err;
    }

    // PreEQ (path-aware)
    if (path->preeq.available) {
        err = dsp_model_update_preeq_path(path_id, &profile->preeq);
        if (err != ESP_OK && first_err == ESP_OK) first_err = err;
    }

    // Out EQ (path-aware)
    if (path->out_eq.available && profile->out_eq_valid) {
        err = dsp_model_update_outeq_path(path_id, &profile->out_eq);
        if (err != ESP_OK && first_err == ESP_OK) first_err = err;
    }

    // DRC (path-aware)
    if (path->drc.available) {
        dsp_drc_view_t requested, confirmed;
        load_drc_view(profile, &requested);
        err = dsp_model_update_drc_view_path(path_id, &requested, &confirmed);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = verify_error(path_id, "drc", path->drc.effect_id,
                "apply_readback", "confirmed", "not_confirmed", err);
        }
    }

    // USB Out Gain (path-aware)
    if (path->usb_out_gain.available) {
        err = dsp_model_set_usb_out_gain(path_id, profile->usb_out_gain);
        if (err != ESP_OK && first_err == ESP_OK) first_err = err;
    }

    if (first_err != ESP_OK) {
        ESP_LOGW(TAG, "DSP-Profil nur teilweise angewendet: %s", esp_err_to_name(first_err));
        return first_err;
    }
    ESP_LOGI(TAG, "DSP-Profil angewendet (Pfad: %s)", path->label);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Readback
// ---------------------------------------------------------------------------

esp_err_t dsp_model_readback(dsp_profile_t *profile)
{
    return dsp_model_readback_path(MVS_PATH_MUSIC, profile);
}

esp_err_t dsp_model_readback_multi(dsp_multi_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    memset(config, 0, sizeof(*config));
    config->schema_version = 2;
    esp_err_t err = dsp_model_readback_path(MVS_PATH_MUSIC, &config->music);
    if (err != ESP_OK) return err;
    if (s_device_profile.paths[MVS_PATH_REC].present) {
        err = dsp_model_readback_path(MVS_PATH_REC, &config->rec);
        config->rec_valid = (err == ESP_OK);
    }
    return ESP_OK;
}

esp_err_t dsp_model_readback_path(mvs_path_id_t path_id, dsp_profile_t *profile)
{
    if (!profile) return ESP_ERR_INVALID_ARG;
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path) return ESP_ERR_NOT_SUPPORTED;

    const dsp_profile_t *stored = path_id == MVS_PATH_REC
        ? &s_rec_profile : &s_music_profile;
    uint16_t stored_delay_ms = stored->delay_ms;
    bool stored_delay_hq = stored->delay_hq_enabled;
    memset(profile, 0, sizeof(*profile));
    ESP_LOGI(TAG, "Lese DSP-Zustand aus (Pfad: %s)...", path->label);

    uint8_t frame[16], report[256];
    uint16_t report_len;
    esp_err_t err;

    // Noise Suppressor
    if (path->noise_suppressor.available) {
        uint8_t ns_id = path->noise_suppressor.effect_id;
        mvs_build_query_frame(ns_id, frame, sizeof(frame));
        mvs_prepare_hid_report(frame, 5, report);
        if (usb_host_ctrl_send_report(report, sizeof(report)) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (usb_host_ctrl_get_report(report, &report_len) == ESP_OK && report_len >= 16)
                mvs_decode_noise_suppressor(report + 5, report_len - 6,
                    &profile->noise_suppressor_enabled, &profile->noise_suppressor_threshold_raw,
                    &profile->noise_suppressor_ratio, &profile->noise_suppressor_attack_ms,
                    &profile->noise_suppressor_release_ms);
        }
    }

    // Virtual Bass
    if (path->virtual_bass.available) {
        uint8_t vb_id = path->virtual_bass.effect_id;
        mvs_build_query_frame(vb_id, frame, sizeof(frame));
        mvs_prepare_hid_report(frame, 5, report);
        if (usb_host_ctrl_send_report(report, sizeof(report)) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (usb_host_ctrl_get_report(report, &report_len) == ESP_OK && report_len >= 10)
                mvs_decode_virtual_bass(report + 5, report_len - 6,
                    &profile->virtual_bass_enabled, &profile->virtual_bass_cutoff_hz,
                    &profile->virtual_bass_intensity_pct, &profile->virtual_bass_enhanced);
        }
    }

    // Silence Detector (nur Music)
    if (path_id == MVS_PATH_MUSIC && s_device_profile.silence_detector.available) {
        uint16_t amplitude = 0;
        dsp_model_read_silence_detector(&profile->silence_detector_enabled, &amplitude);
    }

    // Erweiterte Module
    bool extended_present = false, extended_ok = true;
    if (path->virtual_bass_classic.available) {
        extended_present = true;
        err = dsp_model_read_virtual_bass_classic_path(path_id,
            &profile->virtual_bass_classic_enabled,
            &profile->virtual_bass_classic_cutoff_hz,
            &profile->virtual_bass_classic_intensity_pct);
        if (err != ESP_OK) extended_ok = false;
    }
    if (path->phase.available) {
        extended_present = true;
        err = dsp_model_read_phase(&profile->phase_invert);
        if (err != ESP_OK) extended_ok = false;
    }
    if (path->delay_hq.available) {
        extended_present = true;
        err = dsp_model_read_delay_path(path_id, &profile->delay_enabled,
            &profile->delay_ms, &profile->delay_hq_enabled);
        if (err != ESP_OK) extended_ok = false;
        else if (!profile->delay_enabled) {
            profile->delay_ms = stored_delay_ms;
            profile->delay_hq_enabled = stored_delay_hq;
        }
    }
    profile->phase2_extended_valid = extended_present && extended_ok;

    // PreEQ
    if (path->preeq.available) {
        dsp_model_read_preeq_path(path_id, &profile->preeq);
    }

    // Out EQ
    if (path->out_eq.available) {
        err = dsp_model_read_outeq_path(path_id, &profile->out_eq);
        profile->out_eq_valid = (err == ESP_OK);
    }

    // DRC
    profile->drc_readback_valid = false;
    if (path->drc.available) {
        dsp_drc_view_t view;
        if (dsp_model_read_drc_view_path(path_id, &view) == ESP_OK) {
            store_drc_view(profile, &view);
            profile->drc_readback_valid = true;
        }
    }

    // USB Out Gain
    if (path->usb_out_gain.available) {
        dsp_model_read_usb_out_gain(path_id, &profile->usb_out_gain);
    }

    ESP_LOGI(TAG, "DSP-Readback abgeschlossen (Pfad: %s)", path->label);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// PreEQ Readback (path-aware)
// ---------------------------------------------------------------------------

esp_err_t dsp_model_read_preeq(mvs_preeq_state_t *state)
{
    return dsp_model_read_preeq_path(MVS_PATH_MUSIC, state);
}

esp_err_t dsp_model_read_preeq_path(mvs_path_id_t path_id,
                                     mvs_preeq_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path || !path->preeq.available) return ESP_ERR_NOT_SUPPORTED;

    uint8_t peq_id = path->preeq.effect_id;
    uint8_t frame[5], report[256];
    uint16_t report_len = 0;

    mvs_build_query_frame(peq_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 112 || report[0] != MVS_FRAME_MAGIC_1 ||
        report[1] != MVS_FRAME_MAGIC_2 || report[2] != peq_id ||
        report[4] != 0xFF) return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_preeq(report + 5, report_len - 6, state);
}

// ---------------------------------------------------------------------------
// Out EQ Readback (path-aware)
// ---------------------------------------------------------------------------

esp_err_t dsp_model_read_outeq(mvs_preeq_state_t *state)
{
    return dsp_model_read_outeq_path(MVS_PATH_MUSIC, state);
}

esp_err_t dsp_model_read_outeq_path(mvs_path_id_t path_id,
                                     mvs_preeq_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path || !path->out_eq.available) return ESP_ERR_NOT_SUPPORTED;

    uint8_t eq_id = path->out_eq.effect_id;
    uint8_t frame[5], report[256];
    uint16_t report_len = 0;

    mvs_build_query_frame(eq_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 112 || report[0] != MVS_FRAME_MAGIC_1 ||
        report[1] != MVS_FRAME_MAGIC_2 || report[2] != eq_id ||
        report[4] != 0xFF) return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_preeq(report + 5, report_len - 6, state);
}

// ---------------------------------------------------------------------------
// DRC Readback / Write (path-aware)
// ---------------------------------------------------------------------------

esp_err_t dsp_model_read_drc(mvs_drc_packed_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    if (s_device_profile.drc_schema == MVS_DRC_SCHEMA_A800X_4PATH) {
        return read_drc_a800x_state_id(
            s_device_profile.drc.effect_id, state);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t read_drc_a800x_state_id(
    uint8_t effect_id, mvs_drc_packed_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    uint8_t frame[5], report[256];
    mvs_build_query_frame(effect_id, frame, sizeof(frame));
    esp_err_t last_err = ESP_ERR_INVALID_RESPONSE;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        uint16_t report_len = 0;
        mvs_prepare_hid_report(frame, sizeof(frame), report);
        esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
        if (err != ESP_OK) {
            last_err = err;
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(70));
        err = usb_host_ctrl_get_report(report, &report_len);
        if (err != ESP_OK) {
            last_err = err;
            continue;
        }
        log_full_read("A800X DRC", effect_id, 55, report, report_len);
        uint8_t wire_len = report_len >= 4 ? report[3] : 0;
        if (report_len >= 60 &&
            report[0] == MVS_FRAME_MAGIC_1 &&
            report[1] == MVS_FRAME_MAGIC_2 &&
            report[2] == effect_id &&
            wire_len == 55 &&
            report[4] == 0xFF &&
            report[59] == MVS_FRAME_TERMINATOR) {
            esp_err_t decode = mvs_decode_drc_a800x(
                report + 5, 54, state);
            if (decode == ESP_OK) {
                ESP_LOGI(TAG,
                         "A800X DRC decoded: effect=0x%02X schema=a800x_4path "
                         "payload=54 mode=%u",
                         effect_id, state->mode);
            }
            return decode;
        }
        last_err = ESP_ERR_INVALID_RESPONSE;
    }
    return last_err;
}

static esp_err_t read_drc_state_id(uint8_t effect_id, mvs_drc_state_t *state)
{
    if (!state) return ESP_ERR_NOT_SUPPORTED;
    uint8_t frame[5], report[256];
    mvs_build_query_frame(effect_id, frame, sizeof(frame));
    esp_err_t last_err = ESP_ERR_INVALID_RESPONSE;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        uint16_t report_len = 0;
        mvs_prepare_hid_report(frame, sizeof(frame), report);
        esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
        if (err != ESP_OK) {
            last_err = err;
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(70));
        err = usb_host_ctrl_get_report(report, &report_len);
        if (err != ESP_OK) {
            last_err = err;
            continue;
        }
        log_full_read(effect_id == 0x90 ? "Rec DRC" :
                      effect_id == 0x8F ? "Music DRC" : "DRC",
                      effect_id, 39, report, report_len);
        uint8_t wire_len = report_len >= 4 ? report[3] : 0;
        if (report_len >= 6 && report[0] == MVS_FRAME_MAGIC_1 &&
            report[1] == MVS_FRAME_MAGIC_2 && report[2] == effect_id &&
            wire_len == 39 && (size_t)wire_len + 5U <= report_len &&
            report[4] == 0xFF &&
            report[4U + wire_len] == MVS_FRAME_TERMINATOR) {
            return mvs_decode_drc_state(report + 5, 38, state);
        }
        ESP_LOGW(TAG, "DRC 0x%02X invalid read %u: len=%u wire=%u hdr=%02X %02X %02X %02X %02X",
                 effect_id, attempt + 1U, report_len, wire_len, report[0],
                 report[1], report[2], report[3], report[4]);
        last_err = ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGW(TAG, "DRC 0x%02X read failed after 3 attempts: %s",
             effect_id, esp_err_to_name(last_err));
    return last_err;
}

esp_err_t dsp_model_read_drc_view(dsp_drc_view_t *view)
{
    return dsp_model_read_drc_view_path(MVS_PATH_MUSIC, view);
}

esp_err_t dsp_model_read_drc_view_path(mvs_path_id_t path_id,
                                        dsp_drc_view_t *view)
{
    if (!view) return ESP_ERR_INVALID_ARG;
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path || !path->drc.available) return ESP_ERR_NOT_SUPPORTED;

    if (path->drc_schema == MVS_DRC_SCHEMA_A800X_4PATH) {
        mvs_drc_packed_state_t state;
        esp_err_t err = read_drc_a800x_state_id(
            path->drc.effect_id, &state);
        if (err != ESP_OK) return err;
        return mvs_drc_a800x_to_view(&state, view);
    }
    if (path->drc_schema != MVS_DRC_SCHEMA_UNIFIED_2BAND)
        return ESP_ERR_NOT_SUPPORTED;
    mvs_drc_state_t state;
    esp_err_t err = read_drc_state_id(path->drc.effect_id, &state);
    if (err != ESP_OK) return err;
    return mvs_drc_state_to_view(&state, view);
}

esp_err_t dsp_model_profile_drc_view(const dsp_profile_t *profile,
                                     dsp_drc_view_t *view)
{
    if (!profile || !view) return ESP_ERR_INVALID_ARG;
    if (!profile->drc_readback_valid) return ESP_ERR_INVALID_RESPONSE;
    load_drc_view(profile, view);
    return ESP_OK;
}

static esp_err_t send_u16_array(uint8_t effect_id, uint8_t selector,
                                const uint16_t *values, size_t count)
{
    uint8_t frame[16];
    size_t frame_len = 0;
    esp_err_t err = mvs_build_write_u16_array_frame(effect_id, selector, values,
        count, frame, sizeof(frame), &frame_len);
    if (err != ESP_OK) return err;
    err = send_mvs_command(frame, (uint16_t)frame_len);
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(20));
    return err;
}

esp_err_t dsp_model_update_drc_view(const dsp_drc_view_t *requested,
                                     dsp_drc_view_t *confirmed)
{
    return dsp_model_update_drc_view_path(MVS_PATH_MUSIC, requested, confirmed);
}

esp_err_t dsp_model_set_drc_mode_path(mvs_path_id_t path_id, uint16_t mode,
                                       dsp_drc_view_t *confirmed)
{
    if (!confirmed || mode > 6) return ESP_ERR_INVALID_ARG;
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path || !path->drc.available)
        return ESP_ERR_NOT_SUPPORTED;

    if (path->drc_schema == MVS_DRC_SCHEMA_A800X_4PATH) {
        // Read full state, change mode, write full frame back
        mvs_drc_packed_state_t before;
        esp_err_t err = read_drc_a800x_state_id(path->drc.effect_id, &before);
        if (err != ESP_OK) return err;
        if (before.mode == mode) {
            // No change needed, just convert to view
            return mvs_drc_a800x_to_view(&before, confirmed);
        }
        before.mode = mode;
        uint8_t full_frame[60];
        err = mvs_build_drc_a800x_full_frame(
            path->drc.effect_id, &before, full_frame, sizeof(full_frame));
        if (err == ESP_OK)
            err = send_mvs_command(full_frame, sizeof(full_frame));
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(20));

        mvs_drc_packed_state_t after;
        err = read_drc_a800x_state_id(path->drc.effect_id, &after);
        if (err != ESP_OK) return err;
        if (after.mode != mode) return ESP_ERR_INVALID_RESPONSE;
        return mvs_drc_a800x_to_view(&after, confirmed);
    }

    if (path->drc_schema != MVS_DRC_SCHEMA_UNIFIED_2BAND)
        return ESP_ERR_NOT_SUPPORTED;

    mvs_drc_state_t before;
    esp_err_t err = read_drc_state_id(path->drc.effect_id, &before);
    if (err != ESP_OK) return err;
    if (before.mode != mode) {
        uint8_t frame[8];
        err = mvs_build_write_frame(path->drc.effect_id, 0x02, mode,
                                    frame, sizeof(frame));
        if (err != ESP_OK) return err;
        err = send_mvs_command(frame, sizeof(frame));
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    mvs_drc_state_t after;
    err = read_drc_state_id(path->drc.effect_id, &after);
    if (err != ESP_OK) return err;
    if (after.mode != mode) return ESP_ERR_INVALID_RESPONSE;
    return mvs_drc_state_to_view(&after, confirmed);
}

esp_err_t dsp_model_update_drc_view_path(mvs_path_id_t path_id,
                                          const dsp_drc_view_t *requested,
                                          dsp_drc_view_t *confirmed)
{
    if (!requested || !confirmed) return ESP_ERR_INVALID_ARG;
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path || !path->drc.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t effect_id = path->drc.effect_id;

    if (path->drc_schema == MVS_DRC_SCHEMA_A800X_4PATH) {
        mvs_drc_packed_state_t before;
        esp_err_t err = read_drc_a800x_state_id(effect_id, &before);
        if (err != ESP_OK) return err;
        if (requested->mode > 6) return ESP_ERR_INVALID_ARG;

        // Resolve mode-dependent band visibility
        dsp_drc_view_t current;
        mvs_drc_packed_state_t temp = {.mode = requested->mode};
        mvs_drc_a800x_to_view(&temp, &current);

        mvs_drc_packed_state_t desired = before;
        desired.mode = requested->mode;
        desired.enabled = requested->enabled ? 1U : 0U;

        // Crossover (shared for all multiband modes)
        if (current.crossover_visible) {
            desired.crossover_freq1_hz = requested->crossover_hz;
            desired.crossover_freq2_hz = requested->crossover_hz;
        }
        if (current.q_visible) {
            desired.crossover_q1_raw = (uint16_t)lround(requested->q_lp * 1024.0);
            desired.crossover_q2_raw = (uint16_t)lround(requested->q_hp * 1024.0);
        }

        // Full band (mode 0, modes 4-6)
        if (current.full_band_supported) {
            const dsp_drc_band_view_t *fb =
                &requested->bands[MVS_DRC_BAND_FULL];
            desired.pregains[3] = fb->pregain_db <= -72.0
                ? 0U
                : (uint16_t)lround(
                    4096.0 * pow(10.0, fb->pregain_db / 20.0));
            desired.thresholds[3] =
                (int16_t)lround(fb->threshold_db * 100.0);
            desired.ratios[3] = (uint16_t)lround(fb->ratio * 100.0);
            desired.attacks[3] = fb->attack_ms;
            desired.releases[3] = fb->release_ms;
        }

        // Lower/upper bands (modes 1-6)
        if (current.lower_upper_visible) {
            for (int i = 0; i < 2; ++i) {
                size_t view_idx = i == 0 ? MVS_DRC_BAND_LOWER : MVS_DRC_BAND_UPPER;
                const dsp_drc_band_view_t *b =
                    &requested->bands[view_idx];
                desired.pregains[i] = b->pregain_db <= -72.0
                    ? 0U
                    : (uint16_t)lround(
                        4096.0 * pow(10.0, b->pregain_db / 20.0));
                desired.thresholds[i] =
                    (int16_t)lround(b->threshold_db * 100.0);
                desired.ratios[i] = (uint16_t)lround(b->ratio * 100.0);
                desired.attacks[i] = b->attack_ms;
                desired.releases[i] = b->release_ms;
            }
        }

        uint8_t full_frame[60];
        err = mvs_build_drc_a800x_full_frame(
            effect_id, &desired, full_frame, sizeof(full_frame));
        if (err == ESP_OK)
            err = send_mvs_command(full_frame, sizeof(full_frame));
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(20));

        mvs_drc_packed_state_t after;
        err = read_drc_a800x_state_id(effect_id, &after);
        if (err != ESP_OK) return err;
        if (memcmp(&after, &desired, sizeof(after)) != 0)
            return ESP_ERR_INVALID_RESPONSE;
        return mvs_drc_a800x_to_view(&after, confirmed);
    }

    if (path->drc_schema != MVS_DRC_SCHEMA_UNIFIED_2BAND)
        return ESP_ERR_NOT_SUPPORTED;

    mvs_drc_state_t before;
    esp_err_t err = read_drc_state_id(effect_id, &before);
    if (err != ESP_OK) return err;
    dsp_drc_view_t current;
    mvs_drc_state_t requested_layout = {.mode = requested->mode};
    mvs_drc_state_to_view(&requested_layout, &current);
    if (requested->mode > 6) return ESP_ERR_INVALID_ARG;

    mvs_drc_state_t desired = before;
    desired.mode = requested->mode;
    desired.enabled = requested->enabled ? 1U : 0U;
    if (current.crossover_visible)
        desired.crossover_hz = requested->crossover_hz;
    if (current.q_visible) {
        desired.q_lp_raw = (uint16_t)lround(requested->q_lp * 1024.0);
        desired.q_hp_raw = (uint16_t)lround(requested->q_hp * 1024.0);
    }
    for (size_t i = 0; i < MVS_DRC_BAND_COUNT; ++i) {
        bool visible = i == MVS_DRC_BAND_FULL
            ? current.full_band_supported : current.lower_upper_visible;
        if (!visible) continue;
        desired.thresholds[i] =
            (int16_t)lround(requested->bands[i].threshold_db * 100.0);
        desired.ratios[i] = (uint16_t)lround(requested->bands[i].ratio);
        desired.attacks[i] = requested->bands[i].attack_ms;
        desired.releases[i] = requested->bands[i].release_ms;
    }
    if (current.lower_upper_visible || current.full_band_supported) {
        size_t pregain_band = current.lower_upper_visible
            ? MVS_DRC_BAND_LOWER : MVS_DRC_BAND_FULL;
        desired.pregain_lower = (uint16_t)lround(
            4096.0 * pow(10.0, requested->bands[pregain_band].pregain_db / 20.0));
    }
    if (current.lower_upper_visible) {
        desired.pregain_upper = (uint16_t)lround(
            4096.0 * pow(10.0, requested->bands[MVS_DRC_BAND_UPPER].pregain_db / 20.0));
    }

    if (desired.mode != before.mode) {
        uint8_t frame[8];
        err = mvs_build_write_frame(effect_id, 0x02, desired.mode,
                                    frame, sizeof(frame));
        if (err == ESP_OK) err = send_mvs_command(frame, sizeof(frame));
        if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(20));
    }

    uint16_t values[3];
    for (size_t i = 0; i < 3; ++i) values[i] = (uint16_t)desired.thresholds[i];
    err = send_u16_array(effect_id, 4, values, 3);
    for (size_t i = 0; err == ESP_OK && i < 3; ++i) values[i] = desired.ratios[i];
    if (err == ESP_OK) err = send_u16_array(effect_id, 5, values, 3);
    for (size_t i = 0; err == ESP_OK && i < 3; ++i) values[i] = desired.attacks[i];
    if (err == ESP_OK) err = send_u16_array(effect_id, 6, values, 3);
    for (size_t i = 0; err == ESP_OK && i < 3; ++i) values[i] = desired.releases[i];
    if (err == ESP_OK) err = send_u16_array(effect_id, 7, values, 3);

    const struct { uint8_t selector; uint16_t value; bool write; } scalar[] = {
        {1, desired.crossover_hz, current.crossover_visible},
        {3, desired.q_lp_raw, current.q_visible},
        {3, desired.q_hp_raw, false}, /* selector 3 is written as an array below */
        {8, desired.pregain_lower, true},
        {9, desired.pregain_upper, current.lower_upper_visible},
        {0, desired.enabled, true},
    };
    if (err == ESP_OK && current.q_visible) {
        uint16_t qs[] = {desired.q_lp_raw, desired.q_hp_raw};
        err = send_u16_array(effect_id, 3, qs, 2);
    }
    for (size_t i = 0; err == ESP_OK && i < sizeof(scalar) / sizeof(scalar[0]); ++i) {
        if (!scalar[i].write || scalar[i].selector == 3) continue;
        uint8_t frame[8];
        err = mvs_build_write_frame(effect_id, scalar[i].selector,
                                    scalar[i].value, frame, sizeof(frame));
        if (err == ESP_OK) {
            err = send_mvs_command(frame, sizeof(frame));
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (err != ESP_OK) return err;

    mvs_drc_state_t after;
    err = read_drc_state_id(effect_id, &after);
    if (err != ESP_OK) return err;
    if (memcmp(&after, &desired, sizeof(after)) != 0)
        return ESP_ERR_INVALID_RESPONSE;
    return mvs_drc_state_to_view(&after, confirmed);
}

// ---------------------------------------------------------------------------
// Targeted Read: Noise Suppressor (Legacy Music)
// ---------------------------------------------------------------------------

esp_err_t dsp_model_read_noise_suppressor(bool *enabled, int16_t *threshold_raw,
                                           uint16_t *ratio, uint16_t *attack_ms,
                                           uint16_t *release_ms)
{
    if (!enabled || !threshold_raw || !ratio || !attack_ms || !release_ms)
        return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.noise_suppressor.available) return ESP_ERR_NOT_SUPPORTED;

    uint8_t ns_id = s_device_profile.noise_suppressor.effect_id;
    uint8_t frame[16], report[256];
    uint16_t report_len = 0;
    mvs_build_query_frame(ns_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 16) return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_noise_suppressor(report + 5, report_len - 6,
        enabled, threshold_raw, ratio, attack_ms, release_ms);
}

// ---------------------------------------------------------------------------
// Targeted Read: Virtual Bass (Legacy Music)
// ---------------------------------------------------------------------------

esp_err_t dsp_model_read_virtual_bass(bool *enabled, uint16_t *cutoff_hz,
                                       uint16_t *intensity_pct, bool *enhanced)
{
    if (!enabled || !cutoff_hz || !intensity_pct || !enhanced)
        return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.virtual_bass.available) return ESP_ERR_NOT_SUPPORTED;

    uint8_t vb_id = s_device_profile.virtual_bass.effect_id;
    uint8_t frame[16], report[256];
    uint16_t report_len = 0;
    mvs_build_query_frame(vb_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 10) return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_virtual_bass(report + 5, report_len - 6,
        enabled, cutoff_hz, intensity_pct, enhanced);
}

esp_err_t dsp_model_read_virtual_bass_path(mvs_path_id_t path_id,
    bool *enabled, uint16_t *cutoff_hz, uint16_t *intensity_pct, bool *enhanced)
{
    if (!enabled || !cutoff_hz || !intensity_pct || !enhanced)
        return ESP_ERR_INVALID_ARG;
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path || !path->virtual_bass.available) return ESP_ERR_NOT_SUPPORTED;

    uint8_t vb_id = path->virtual_bass.effect_id;
    uint8_t frame[16], report[256];
    uint16_t report_len = 0;
    mvs_build_query_frame(vb_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 10) return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_virtual_bass(report + 5, report_len - 6,
        enabled, cutoff_hz, intensity_pct, enhanced);
}

esp_err_t dsp_model_set_virtual_bass_path(mvs_path_id_t path_id, bool enable,
    uint16_t cutoff_hz, uint16_t intensity_pct, bool bass_enhanced)
{
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path || !path->virtual_bass.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t vb_id = path->virtual_bass.effect_id;
    uint8_t frame[8];

    esp_err_t err = mvs_build_write_frame(vb_id, 0, enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    err = send_mvs_command(frame, sizeof(frame));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (enable) {
        err = mvs_build_write_frame(vb_id, 1, cutoff_hz, frame, sizeof(frame));
        if (err == ESP_OK) { err = send_mvs_command(frame, sizeof(frame)); vTaskDelay(pdMS_TO_TICKS(20)); }
        if (err == ESP_OK) err = mvs_build_write_frame(vb_id, 2, intensity_pct, frame, sizeof(frame));
        if (err == ESP_OK) { err = send_mvs_command(frame, sizeof(frame)); vTaskDelay(pdMS_TO_TICKS(20)); }
        if (err == ESP_OK) err = mvs_build_write_frame(vb_id, 3, bass_enhanced ? 1 : 0, frame, sizeof(frame));
        if (err == ESP_OK) { err = send_mvs_command(frame, sizeof(frame)); vTaskDelay(pdMS_TO_TICKS(20)); }
    }
    return err;
}

// ---------------------------------------------------------------------------
// Profil-Helper: DRC View → Profile
// ---------------------------------------------------------------------------

void dsp_model_profile_apply_drc_view(dsp_profile_t *profile,
                                       const dsp_drc_view_t *view)
{
    if (!profile || !view) return;
    memset(&profile->drc, 0, sizeof(profile->drc));
    store_drc_view(profile, view);
}

// ---------------------------------------------------------------------------
// Verify Full Profile (Legacy Music)
// ---------------------------------------------------------------------------

esp_err_t dsp_model_verify_full_profile(const dsp_profile_t *expected)
{
    return dsp_model_verify_path_profile(MVS_PATH_MUSIC, expected);
}

esp_err_t dsp_model_verify_multi_config(const dsp_multi_config_t *expected)
{
    if (!expected) return ESP_ERR_INVALID_ARG;
    s_verify_mismatch[0] = '\0';
    esp_err_t err = dsp_model_verify_path_profile(MVS_PATH_MUSIC, &expected->music);
    if (err != ESP_OK) return err;
    if (expected->rec_valid && s_device_profile.paths[MVS_PATH_REC].present) {
        err = dsp_model_verify_path_profile(MVS_PATH_REC, &expected->rec);
    }
    return err;
}

esp_err_t dsp_model_verify_path_profile(mvs_path_id_t path_id,
                                         const dsp_profile_t *expected)
{
    if (!expected) return ESP_ERR_INVALID_ARG;
    const mvs_effect_path_t *dev = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!dev) return ESP_ERR_NOT_SUPPORTED;

    // Noise Suppressor
    if (dev->noise_suppressor.available) {
        bool en; int16_t thr; uint16_t rat, atk, rel;
        esp_err_t err = dsp_model_read_noise_suppressor(&en, &thr, &rat, &atk, &rel);
        if (err != ESP_OK) return err;
        if (en != expected->noise_suppressor_enabled) return ESP_ERR_INVALID_RESPONSE;
        if (expected->noise_suppressor_enabled &&
            (thr != expected->noise_suppressor_threshold_raw ||
             rat != expected->noise_suppressor_ratio ||
             atk != expected->noise_suppressor_attack_ms ||
             rel != expected->noise_suppressor_release_ms))
            return ESP_ERR_INVALID_RESPONSE;
    }

    // Virtual Bass
    if (dev->virtual_bass.available) {
        bool en, enh; uint16_t cut, it;
        esp_err_t err = dsp_model_read_virtual_bass_path(
            path_id, &en, &cut, &it, &enh);
        if (err != ESP_OK)
            return verify_mismatch_i64(path_id, "virtual_bass",
                dev->virtual_bass.effect_id, "read_error", ESP_OK, err);
        if (en != expected->virtual_bass_enabled)
            return verify_mismatch_i64(path_id, "virtual_bass",
                dev->virtual_bass.effect_id, "enabled",
                expected->virtual_bass_enabled, en);
        if (expected->virtual_bass_enabled) {
            if (cut != expected->virtual_bass_cutoff_hz)
                return verify_mismatch_i64(path_id, "virtual_bass",
                    dev->virtual_bass.effect_id, "cutoff_hz",
                    expected->virtual_bass_cutoff_hz, cut);
            if (it != expected->virtual_bass_intensity_pct)
                return verify_mismatch_i64(path_id, "virtual_bass",
                    dev->virtual_bass.effect_id, "intensity_pct",
                    expected->virtual_bass_intensity_pct, it);
            if (enh != expected->virtual_bass_enhanced)
                return verify_mismatch_i64(path_id, "virtual_bass",
                    dev->virtual_bass.effect_id, "enhanced",
                    expected->virtual_bass_enhanced, enh);
        }
    }

    // VB Classic (path-aware)
    if (expected->phase2_extended_valid && dev->virtual_bass_classic.available) {
        bool en; uint16_t cut, it;
        esp_err_t err = dsp_model_read_virtual_bass_classic_path(path_id, &en, &cut, &it);
        if (err != ESP_OK) return err;
        if (en != expected->virtual_bass_classic_enabled) return ESP_ERR_INVALID_RESPONSE;
        if (expected->virtual_bass_classic_enabled &&
            (cut != expected->virtual_bass_classic_cutoff_hz ||
             it != expected->virtual_bass_classic_intensity_pct))
            return ESP_ERR_INVALID_RESPONSE;
    }

    // Phase
    if (expected->phase2_extended_valid && dev->phase.available) {
        bool in;
        esp_err_t err = dsp_model_read_phase(&in);
        if (err != ESP_OK) return err;
        if (in != expected->phase_invert) return ESP_ERR_INVALID_RESPONSE;
    }

    // Delay (path-aware)
    if (expected->phase2_extended_valid && dev->delay_hq.available) {
        bool en, hq; uint16_t ms;
        esp_err_t err = dsp_model_read_delay_path(path_id, &en, &ms, &hq);
        if (err != ESP_OK) return err;
        if (en != expected->delay_enabled) return ESP_ERR_INVALID_RESPONSE;
        if (expected->delay_enabled && (ms != expected->delay_ms || hq != expected->delay_hq_enabled))
            return ESP_ERR_INVALID_RESPONSE;
    }

    // PreEQ
    if (dev->preeq.available) {
        mvs_preeq_state_t state;
        esp_err_t err = dsp_model_read_preeq_path(path_id, &state);
        if (err != ESP_OK) return err;
#define VERIFY_PREEQ_FIELD(field_) do { \
    if (state.field_ != expected->preeq.field_) \
        return verify_mismatch_i64(path_id, "preeq", dev->preeq.effect_id, \
            #field_, expected->preeq.field_, state.field_); \
} while (0)
        VERIFY_PREEQ_FIELD(block_enabled);
        VERIFY_PREEQ_FIELD(pre_gain_raw);
#undef VERIFY_PREEQ_FIELD
        for (size_t i = 0; i < 10; ++i) {
            const mvs_preeq_filter_t *actual = &state.filters[i];
            const mvs_preeq_filter_t *exp = &expected->preeq.filters[i];
            char field[40];
#define VERIFY_PREEQ_FILTER_FIELD(member_) do { \
    if (actual->member_ != exp->member_) { \
        snprintf(field, sizeof(field), "filters[%u].%s", \
                 (unsigned)i, #member_); \
        return verify_mismatch_i64(path_id, "preeq", dev->preeq.effect_id, \
            field, exp->member_, actual->member_); \
    } \
} while (0)
            VERIFY_PREEQ_FILTER_FIELD(enabled);
            VERIFY_PREEQ_FILTER_FIELD(type);
            VERIFY_PREEQ_FILTER_FIELD(frequency_hz);
            VERIFY_PREEQ_FILTER_FIELD(q_raw);
            VERIFY_PREEQ_FILTER_FIELD(gain_raw);
#undef VERIFY_PREEQ_FILTER_FIELD
        }
    }

    // Out EQ
    if (dev->out_eq.available && expected->out_eq_valid) {
        mvs_preeq_state_t state;
        esp_err_t err = dsp_model_read_outeq_path(path_id, &state);
        if (err != ESP_OK) return err;
#define VERIFY_OUTEQ_FIELD(field_) do { \
    if (state.field_ != expected->out_eq.field_) \
        return verify_mismatch_i64(path_id, "out_eq", dev->out_eq.effect_id, \
            #field_, expected->out_eq.field_, state.field_); \
} while (0)
        VERIFY_OUTEQ_FIELD(block_enabled);
        VERIFY_OUTEQ_FIELD(pre_gain_raw);
#undef VERIFY_OUTEQ_FIELD
        for (size_t i = 0; i < 10; ++i) {
            const mvs_preeq_filter_t *actual = &state.filters[i];
            const mvs_preeq_filter_t *exp = &expected->out_eq.filters[i];
            char field[40];
#define VERIFY_OUTEQ_FILTER_FIELD(member_) do { \
    if (actual->member_ != exp->member_) { \
        snprintf(field, sizeof(field), "filters[%u].%s", \
                 (unsigned)i, #member_); \
        return verify_mismatch_i64(path_id, "out_eq", dev->out_eq.effect_id, \
            field, exp->member_, actual->member_); \
    } \
} while (0)
            VERIFY_OUTEQ_FILTER_FIELD(enabled);
            VERIFY_OUTEQ_FILTER_FIELD(type);
            VERIFY_OUTEQ_FILTER_FIELD(frequency_hz);
            VERIFY_OUTEQ_FILTER_FIELD(q_raw);
            VERIFY_OUTEQ_FILTER_FIELD(gain_raw);
#undef VERIFY_OUTEQ_FILTER_FIELD
        }
    }

    // DRC
    if (dev->drc.available) {
        dsp_drc_view_t view;
        esp_err_t err = dsp_model_read_drc_view_path(path_id, &view);
        if (err != ESP_OK) {
            return verify_error(path_id, "drc", dev->drc.effect_id,
                "full_read", "readable", "not_read", err);
        }
        dsp_drc_view_t exp;
        load_drc_view(expected, &exp);
#define VERIFY_DRC_I64(field_, expected_, actual_) do { \
    if ((long long)(expected_) != (long long)(actual_)) \
        return verify_mismatch_i64(path_id, "drc", dev->drc.effect_id, \
            field_, (long long)(expected_), (long long)(actual_)); \
} while (0)
        VERIFY_DRC_I64("enabled", exp.enabled, view.enabled);
        VERIFY_DRC_I64("mode", exp.mode, view.mode);
        if (exp.crossover_visible) {
            VERIFY_DRC_I64("crossover_hz", exp.crossover_hz, view.crossover_hz);
        }
        if (exp.q_visible) {
            VERIFY_DRC_I64("q_lp_raw", lround(exp.q_lp * 1024.0),
                           lround(view.q_lp * 1024.0));
            VERIFY_DRC_I64("q_hp_raw", lround(exp.q_hp * 1024.0),
                           lround(view.q_hp * 1024.0));
        }
        for (size_t i = 0; i < MVS_DRC_BAND_COUNT; ++i) {
            if ((i == MVS_DRC_BAND_FULL && !exp.full_band_supported) ||
                (i != MVS_DRC_BAND_FULL && !exp.lower_upper_visible)) {
                continue;
            }
            char field[48];
#define VERIFY_DRC_BAND_DOUBLE(member_, scale_) do { \
    snprintf(field, sizeof(field), "bands[%u].%s", \
             (unsigned)i, #member_); \
    VERIFY_DRC_I64(field, lround(exp.bands[i].member_ * (scale_)), \
                   lround(view.bands[i].member_ * (scale_))); \
} while (0)
            VERIFY_DRC_BAND_DOUBLE(pregain_db, 100.0);
            VERIFY_DRC_BAND_DOUBLE(threshold_db, 100.0);
            VERIFY_DRC_BAND_DOUBLE(ratio, 100.0);
#undef VERIFY_DRC_BAND_DOUBLE
            snprintf(field, sizeof(field), "bands[%u].attack_ms", (unsigned)i);
            VERIFY_DRC_I64(field, exp.bands[i].attack_ms,
                           view.bands[i].attack_ms);
            snprintf(field, sizeof(field), "bands[%u].release_ms", (unsigned)i);
            VERIFY_DRC_I64(field, exp.bands[i].release_ms,
                           view.bands[i].release_ms);
        }
#undef VERIFY_DRC_I64
    }

    // USB Out Gain
    if (dev->usb_out_gain.available) {
        uint16_t gain;
        esp_err_t err = dsp_model_read_usb_out_gain(path_id, &gain);
        if (err != ESP_OK) return err;
        if (gain != expected->usb_out_gain) return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Virtual Bass Classic (path-aware)
// ---------------------------------------------------------------------------

esp_err_t dsp_model_read_virtual_bass_classic(bool *enable, uint16_t *cutoff_hz,
                                               uint16_t *intensity_pct)
{
    return dsp_model_read_virtual_bass_classic_path(MVS_PATH_MUSIC,
        enable, cutoff_hz, intensity_pct);
}

esp_err_t dsp_model_read_virtual_bass_classic_path(mvs_path_id_t path_id,
    bool *enable, uint16_t *cutoff_hz, uint16_t *intensity_pct)
{
    if (!enable || !cutoff_hz || !intensity_pct) return ESP_ERR_INVALID_ARG;
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path || !path->virtual_bass_classic.available) return ESP_ERR_NOT_SUPPORTED;

    uint8_t vb_id = path->virtual_bass_classic.effect_id;
    uint8_t frame[16], report[256];
    uint16_t report_len = 0;
    mvs_build_query_frame(vb_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 11) return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_virtual_bass_classic(report + 5, report_len - 6,
        enable, cutoff_hz, intensity_pct);
}

esp_err_t dsp_model_set_virtual_bass_classic_state(bool enable,
    uint16_t cutoff_hz, uint16_t intensity_pct)
{
    return dsp_model_set_virtual_bass_classic_path(MVS_PATH_MUSIC,
        enable, cutoff_hz, intensity_pct);
}

esp_err_t dsp_model_set_virtual_bass_classic_path(mvs_path_id_t path_id,
    bool enable, uint16_t cutoff_hz, uint16_t intensity_pct)
{
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path || !path->virtual_bass_classic.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t vb_id = path->virtual_bass_classic.effect_id;

    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(vb_id, 0, enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    err = send_mvs_command(frame, sizeof(frame));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (enable) {
        err = mvs_build_write_frame(vb_id, 1, cutoff_hz, frame, sizeof(frame));
        if (err == ESP_OK) { err = send_mvs_command(frame, sizeof(frame)); vTaskDelay(pdMS_TO_TICKS(20)); }
        if (err == ESP_OK) err = mvs_build_write_frame(vb_id, 2, intensity_pct, frame, sizeof(frame));
        if (err == ESP_OK) { err = send_mvs_command(frame, sizeof(frame)); vTaskDelay(pdMS_TO_TICKS(20)); }
    }
    return err;
}

// ---------------------------------------------------------------------------
// Delay (path-aware)
// ---------------------------------------------------------------------------

esp_err_t dsp_model_read_delay(bool *enable, uint16_t *delay_ms, bool *hq_enabled)
{
    return dsp_model_read_delay_path(MVS_PATH_MUSIC, enable, delay_ms, hq_enabled);
}

esp_err_t dsp_model_read_delay_path(mvs_path_id_t path_id,
    bool *enable, uint16_t *delay_ms, bool *hq_enabled)
{
    if (!enable || !delay_ms || !hq_enabled) return ESP_ERR_INVALID_ARG;
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path || !path->delay_hq.available) return ESP_ERR_NOT_SUPPORTED;

    uint8_t delay_id = path->delay_hq.effect_id;
    uint8_t frame[16], report[256];
    uint16_t report_len = 0;
    mvs_build_query_frame(delay_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    uint8_t wire_len = report_len >= 4 ? report[3] : 0;
    ESP_LOGI(TAG, "Delay raw path=%s effect=0x%02X report_len=%u wire_len=%u",
             path->label, delay_id, report_len, wire_len);
    size_t raw_len = (size_t)wire_len + 5U <= report_len
        ? (size_t)wire_len + 5U : report_len;
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, report,
        raw_len < sizeof(report) ? raw_len : sizeof(report), ESP_LOG_INFO);
    if (report_len < 14 || report[0] != MVS_FRAME_MAGIC_1 ||
        report[1] != MVS_FRAME_MAGIC_2 || report[2] != delay_id ||
        report[4] != 0xFF || wire_len != 9 ||
        (size_t)wire_len + 5U > report_len ||
        report[4U + wire_len] != MVS_FRAME_TERMINATOR)
        return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_delay(report + 5, wire_len - 1U,
                            enable, delay_ms, hq_enabled);
}

esp_err_t dsp_model_set_delay(bool enable, uint16_t delay_ms, bool hq_enabled)
{
    return dsp_model_set_delay_path(MVS_PATH_MUSIC, enable, delay_ms, hq_enabled);
}

esp_err_t dsp_model_set_delay_path(mvs_path_id_t path_id,
    bool enable, uint16_t delay_ms, bool hq_enabled)
{
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path || !path->delay_hq.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t delay_id = path->delay_hq.effect_id;

    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(delay_id, 0, enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    err = send_mvs_command(frame, sizeof(frame));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (enable) {
        err = mvs_build_write_frame(delay_id, 1, delay_ms, frame, sizeof(frame));
        if (err == ESP_OK) { err = send_mvs_command(frame, sizeof(frame)); vTaskDelay(pdMS_TO_TICKS(20)); }
        if (err == ESP_OK) err = mvs_build_write_frame(delay_id, 2, delay_ms, frame, sizeof(frame));
        if (err == ESP_OK) { err = send_mvs_command(frame, sizeof(frame)); vTaskDelay(pdMS_TO_TICKS(20)); }
        if (err == ESP_OK) err = mvs_build_write_frame(delay_id, 3, hq_enabled ? 1 : 0, frame, sizeof(frame));
        if (err == ESP_OK) { err = send_mvs_command(frame, sizeof(frame)); vTaskDelay(pdMS_TO_TICKS(20)); }
    }
    return err;
}

// ---------------------------------------------------------------------------
// PreEQ Update (path-aware)
// ---------------------------------------------------------------------------

esp_err_t dsp_model_update_preeq(const mvs_preeq_state_t *state)
{
    return dsp_model_update_preeq_path(MVS_PATH_MUSIC, state);
}

esp_err_t dsp_model_update_preeq_path(mvs_path_id_t path_id,
                                       const mvs_preeq_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path || !path->preeq.available) return ESP_ERR_NOT_SUPPORTED;

    uint8_t peq_id = path->preeq.effect_id;
    uint8_t frame[112];

    // Normalisieren wie in den API-Handlern
    mvs_preeq_state_t normalized = *state;
    for (int i = 0; i < 10; i++) {
        mvs_preeq_filter_t *f = &normalized.filters[i];
        if (!f->enabled && f->frequency_hz == 0 && f->q_raw == 0) {
            f->type = MVS_FILTER_PK;
            f->frequency_hz = 20000;
            f->q_raw = 724;
            f->gain_raw = 0;
        }
    }
    mvs_prepare_preeq_for_schema(path->preeq_schema, &normalized);

    esp_err_t err = mvs_build_preeq_full_frame_dyn(peq_id, &normalized,
                                                    frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, sizeof(frame));
}

// ---------------------------------------------------------------------------
// Out EQ Update (path-aware)
// ---------------------------------------------------------------------------

esp_err_t dsp_model_update_outeq_path(mvs_path_id_t path_id,
                                       const mvs_preeq_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path || !path->out_eq.available) return ESP_ERR_NOT_SUPPORTED;

    uint8_t eq_id = path->out_eq.effect_id;
    uint8_t frame[112];

    mvs_preeq_state_t normalized = *state;
    for (int i = 0; i < 10; i++) {
        mvs_preeq_filter_t *f = &normalized.filters[i];
        if (!f->enabled && f->frequency_hz == 0 && f->q_raw == 0) {
            f->type = MVS_FILTER_PK;
            f->frequency_hz = 20000;
            f->q_raw = 724;
            f->gain_raw = 0;
        }
    }
    mvs_prepare_preeq_for_schema(path->out_eq_schema, &normalized);

    esp_err_t err = mvs_build_preeq_full_frame_dyn(eq_id, &normalized,
                                                    frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, sizeof(frame));
}

// ---------------------------------------------------------------------------
// USB Out Gain (path-aware)
// ---------------------------------------------------------------------------

esp_err_t dsp_model_read_usb_out_gain(mvs_path_id_t path_id, uint16_t *gain_raw)
{
    if (!gain_raw) return ESP_ERR_INVALID_ARG;
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path || !path->usb_out_gain.available) return ESP_ERR_NOT_SUPPORTED;

    uint8_t ug_id = path->usb_out_gain.effect_id;
    uint8_t frame[5], report[256];
    uint16_t report_len = 0;
    mvs_build_query_frame(ug_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    log_full_read("USB Out Gain", ug_id, 7, report, report_len);
    if (report_len < 12 || report[0] != MVS_FRAME_MAGIC_1 ||
        report[1] != MVS_FRAME_MAGIC_2 || report[2] != ug_id ||
        report[3] != 7 || report[4] != 0xFF ||
        report[11] != MVS_FRAME_TERMINATOR)
        return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_usb_out_gain(report + 5, 6, gain_raw);
}

esp_err_t dsp_model_set_usb_out_gain(mvs_path_id_t path_id, uint16_t gain_raw)
{
    const mvs_effect_path_t *path = mvs_device_profile_get_path(
        &s_device_profile, path_id);
    if (!path || !path->usb_out_gain.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t ug_id = path->usb_out_gain.effect_id;

    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(ug_id, MVS_SEL_USB_GAIN_OUTPUT,
                                           gain_raw, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, sizeof(frame));
}

// ---------------------------------------------------------------------------
// Remainder functions (keepers from original)
// ---------------------------------------------------------------------------

esp_err_t dsp_model_set_noise_suppressor(bool enable)
{
    return dsp_model_set_noise_suppressor_state(enable, -5500, 4, 2, 100);
}

esp_err_t dsp_model_set_noise_suppressor_state(bool enable, int16_t threshold_raw,
                                                uint16_t ratio, uint16_t attack_ms,
                                                uint16_t release_ms)
{
    if (!s_device_profile.noise_suppressor.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t ns_id = s_device_profile.noise_suppressor.effect_id;
    uint8_t frame[8];

    esp_err_t err = mvs_build_write_frame(ns_id, 0, enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    err = send_mvs_command(frame, sizeof(frame));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (enable) {
        err = mvs_build_write_frame(ns_id, 1, (uint16_t)threshold_raw, frame, sizeof(frame));
        if (err == ESP_OK) { err = send_mvs_command(frame, sizeof(frame)); vTaskDelay(pdMS_TO_TICKS(20)); }
        if (err == ESP_OK) err = mvs_build_write_frame(ns_id, 2, ratio, frame, sizeof(frame));
        if (err == ESP_OK) { err = send_mvs_command(frame, sizeof(frame)); vTaskDelay(pdMS_TO_TICKS(20)); }
        if (err == ESP_OK) err = mvs_build_write_frame(ns_id, 3, attack_ms, frame, sizeof(frame));
        if (err == ESP_OK) { err = send_mvs_command(frame, sizeof(frame)); vTaskDelay(pdMS_TO_TICKS(20)); }
        if (err == ESP_OK) err = mvs_build_write_frame(ns_id, 4, release_ms, frame, sizeof(frame));
        if (err == ESP_OK) { err = send_mvs_command(frame, sizeof(frame)); vTaskDelay(pdMS_TO_TICKS(20)); }
    }
    return err;
}

esp_err_t dsp_model_set_virtual_bass(bool enable)
{
    return dsp_model_set_virtual_bass_state(enable, 42, 4, true);
}

esp_err_t dsp_model_set_virtual_bass_state(bool enable, uint16_t cutoff_hz,
                                            uint16_t intensity_pct, bool bass_enhanced)
{
    if (!s_device_profile.virtual_bass.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t vb_id = s_device_profile.virtual_bass.effect_id;
    uint8_t frame[8];

    esp_err_t err = mvs_build_write_frame(vb_id, 0, enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    err = send_mvs_command(frame, sizeof(frame));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (enable) {
        err = mvs_build_write_frame(vb_id, 1, cutoff_hz, frame, sizeof(frame));
        if (err == ESP_OK) { err = send_mvs_command(frame, sizeof(frame)); vTaskDelay(pdMS_TO_TICKS(20)); }
        if (err == ESP_OK) err = mvs_build_write_frame(vb_id, 2, intensity_pct, frame, sizeof(frame));
        if (err == ESP_OK) { err = send_mvs_command(frame, sizeof(frame)); vTaskDelay(pdMS_TO_TICKS(20)); }
        if (err == ESP_OK) err = mvs_build_write_frame(vb_id, 3, bass_enhanced ? 1 : 0, frame, sizeof(frame));
        if (err == ESP_OK) { err = send_mvs_command(frame, sizeof(frame)); vTaskDelay(pdMS_TO_TICKS(20)); }
    }
    return err;
}

esp_err_t dsp_model_read_phase(bool *phase_invert)
{
    if (!phase_invert) return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.phase.available) return ESP_ERR_NOT_SUPPORTED;

    uint8_t phase_id = s_device_profile.phase.effect_id;
    uint8_t frame[16], report[256];
    uint16_t report_len = 0;
    mvs_build_query_frame(phase_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    uint8_t wire_len = report_len >= 4 ? report[3] : 0;
    ESP_LOGI(TAG, "Phase raw path=Music effect=0x%02X report_len=%u wire_len=%u",
             phase_id, report_len, wire_len);
    size_t raw_len = (size_t)wire_len + 5U <= report_len
        ? (size_t)wire_len + 5U : report_len;
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, report,
        raw_len < sizeof(report) ? raw_len : sizeof(report), ESP_LOG_INFO);
    if (report_len < 8 || report[0] != MVS_FRAME_MAGIC_1 ||
        report[1] != MVS_FRAME_MAGIC_2 || report[2] != phase_id ||
        report[4] != 0xFF || (wire_len != 3 && wire_len != 5) ||
        (size_t)wire_len + 5U > report_len ||
        report[4U + wire_len] != MVS_FRAME_TERMINATOR)
        return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_phase(report + 5, wire_len - 1U, phase_invert);
}

esp_err_t dsp_model_set_phase(bool phase_invert)
{
    if (!s_device_profile.phase.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t phase_id = s_device_profile.phase.effect_id;
    uint8_t frame[8];
    uint8_t selector = s_device_profile.phase.state_size == 4 ? 1U : 0U;
    esp_err_t err = mvs_build_write_frame(phase_id, selector,
                                           phase_invert ? 1 : 0,
                                           frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, sizeof(frame));
}

esp_err_t dsp_model_set_silence_detector(bool enable)
{
    if (!s_device_profile.silence_detector.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t sd_id = s_device_profile.silence_detector.effect_id;
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(sd_id, 0, enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, sizeof(frame));
}

esp_err_t dsp_model_read_silence_detector(bool *enabled, uint16_t *amplitude)
{
    if (!enabled || !amplitude) return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.silence_detector.available) return ESP_ERR_NOT_SUPPORTED;

    uint8_t sd_id = s_device_profile.silence_detector.effect_id;
    uint8_t frame[16], report[256];
    uint16_t report_len = 0;
    mvs_build_query_frame(sd_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 9) return ESP_ERR_INVALID_RESPONSE;
    // Silence: enable(2) + amplitude(2)
    *enabled = (report[5] | (report[6] << 8)) != 0;
    *amplitude = (uint16_t)(report[7] | (report[8] << 8));
    return ESP_OK;
}

esp_err_t dsp_model_read_effect_enabled(uint8_t effect_id, bool *enabled)
{
    if (!enabled) return ESP_ERR_INVALID_ARG;
    uint8_t frame[5], report[256];
    uint16_t report_len = 0;
    mvs_build_query_frame(effect_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 7) return ESP_ERR_INVALID_RESPONSE;
    // First uint16 after header is enable
    *enabled = read_u16_le(report + 5) != 0;
    return ESP_OK;
}

esp_err_t dsp_model_set_preeq_enable(bool enable)
{
    mvs_preeq_state_t state;
    esp_err_t err = dsp_model_read_preeq(&state);
    if (err != ESP_OK) return err;
    state.block_enabled = enable ? 1 : 0;
    return dsp_model_update_preeq(&state);
}

esp_err_t dsp_model_set_drc_enable(bool enable)
{
    dsp_drc_view_t state, confirmed;
    esp_err_t err = dsp_model_read_drc_view(&state);
    if (err != ESP_OK) return err;
    state.enabled = enable ? 1 : 0;
    return dsp_model_update_drc_view(&state, &confirmed);
}

esp_err_t dsp_model_update_drc(const mvs_drc_packed_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    dsp_profile_t profile = {0};
    profile.drc = *state;
    profile.drc_readback_valid = true;
    dsp_drc_view_t requested, confirmed;
    load_drc_view(&profile, &requested);
    return dsp_model_update_drc_view(&requested, &confirmed);
}

static void load_drc_view(const dsp_profile_t *profile, dsp_drc_view_t *view)
{
    if (!profile || !view) return;
    memset(view, 0, sizeof(*view));
    view->valid = true;
    view->enabled = profile->drc.enabled != 0;
    view->mode = profile->drc.mode;
    view->crossover_hz = profile->drc.crossover_freq1_hz;
    view->q_lp = profile->drc.crossover_q1_raw / 1024.0;
    view->q_hp = profile->drc.crossover_q2_raw / 1024.0;
    mvs_drc_state_t layout = {.mode = view->mode};
    dsp_drc_view_t flags;
    mvs_drc_state_to_view(&layout, &flags);
    view->lower_upper_visible = flags.lower_upper_visible;
    view->full_band_supported = flags.full_band_supported;
    view->crossover_visible = flags.crossover_visible;
    view->q_visible = flags.q_visible;
    for (size_t i = 0; i < MVS_DRC_BAND_COUNT; ++i) {
        size_t src = i == MVS_DRC_BAND_FULL ? 3 : i;
        view->bands[i].pregain_db = profile->drc.pregains[src] > 0
            ? 20.0 * log10(profile->drc.pregains[src] / 4096.0) : -72.0;
        view->bands[i].threshold_db = profile->drc.thresholds[src] / 100.0;
        view->bands[i].ratio = profile->drc.ratios[src] / 100.0;
        view->bands[i].attack_ms = profile->drc.attacks[src];
        view->bands[i].release_ms = profile->drc.releases[src];
    }
}

static void store_drc_view(dsp_profile_t *profile, const dsp_drc_view_t *view)
{
    if (!profile || !view) return;
    memset(&profile->drc, 0, sizeof(profile->drc));
    profile->drc.enabled = view->enabled ? 1U : 0U;
    profile->drc.mode = view->mode;
    profile->drc.crossover_freq1_hz = view->crossover_hz;
    profile->drc.crossover_q1_raw = (uint16_t)lround(view->q_lp * 1024.0);
    profile->drc.crossover_q2_raw = (uint16_t)lround(view->q_hp * 1024.0);
    for (size_t i = 0; i < MVS_DRC_BAND_COUNT; ++i) {
        size_t dst = i == MVS_DRC_BAND_FULL ? 3 : i;
        profile->drc.thresholds[dst] =
            (int16_t)lround(view->bands[i].threshold_db * 100.0);
        profile->drc.ratios[dst] =
            (uint16_t)lround(view->bands[i].ratio * 100.0);
        profile->drc.attacks[dst] = view->bands[i].attack_ms;
        profile->drc.releases[dst] = view->bands[i].release_ms;
        profile->drc.pregains[dst] =
            view->bands[i].pregain_db <= -72.0
                ? 0U
                : (uint16_t)lround(
                    4096.0 *
                    pow(10.0, view->bands[i].pregain_db / 20.0));
    }
}

static inline uint16_t read_u16_le(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}
