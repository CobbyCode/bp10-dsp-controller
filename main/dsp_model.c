// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// dsp_model.c — DSP-Modell — Zustand und Parameter
//
// v2: Multi-Path-Unterstützung (Music / REC)

#include "dsp_model.h"
#include <string.h>
#include <math.h>
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

// ---------------------------------------------------------------------------
// Forward declarations für Funktionen, die vor ihrer Definition verwendet werden
static esp_err_t read_drc_classic_id(uint8_t effect_id,
                                     mvs_drc_classic_state_t *state);
static inline uint16_t read_u16_le(const uint8_t *buf);
static void load_drc_view(const dsp_profile_t *profile, dsp_drc_view_t *view);
static void store_drc_view(dsp_profile_t *profile, const dsp_drc_view_t *view);

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
        if (path->drc_schema == MVS_DRC_SCHEMA_A800X_4PATH) {
            err = dsp_model_update_drc(&profile->drc);
        } else {
            dsp_drc_view_t requested, confirmed;
            load_drc_view(profile, &requested);
            err = dsp_model_update_drc_view_path(path_id, &requested, &confirmed);
        }
        if (err != ESP_OK && first_err == ESP_OK) first_err = err;
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
        if (path->drc_schema == MVS_DRC_SCHEMA_A800X_4PATH) {
            profile->drc_readback_valid =
                dsp_model_read_drc(&profile->drc) == ESP_OK;
        } else {
            dsp_drc_view_t view;
            if (dsp_model_read_drc_view_path(path_id, &view) == ESP_OK) {
                store_drc_view(profile, &view);
                profile->drc_readback_valid = true;
            }
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
    if (!s_device_profile.drc.available ||
        s_device_profile.drc_schema != MVS_DRC_SCHEMA_A800X_4PATH)
        return ESP_ERR_NOT_SUPPORTED;

    uint8_t drc_id = s_device_profile.drc.effect_id;
    uint8_t frame[5], report[256];
    uint16_t report_len = 0;
    mvs_build_query_frame(drc_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, sizeof(frame), report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 60 || report[0] != MVS_FRAME_MAGIC_1 ||
        report[1] != MVS_FRAME_MAGIC_2 || report[2] != drc_id ||
        report[4] != 0xFF || report[59] != MVS_FRAME_TERMINATOR)
        return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_drc_a800x(report + 5, report_len - 6, state);
}

static esp_err_t read_drc_classic(mvs_drc_classic_state_t *state)
{
    return read_drc_classic_id(s_device_profile.drc.effect_id, state);
}

static esp_err_t read_drc_classic_id(uint8_t effect_id,
                                      mvs_drc_classic_state_t *state)
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
        uint8_t wire_len = report_len >= 4 ? report[3] : 0;
        if (report_len >= 6 && report[0] == MVS_FRAME_MAGIC_1 &&
            report[1] == MVS_FRAME_MAGIC_2 && report[2] == effect_id &&
            wire_len == 39 && (size_t)wire_len + 5U <= report_len &&
            report[4] == 0xFF &&
            report[4U + wire_len] == MVS_FRAME_TERMINATOR) {
            return mvs_decode_drc_classic(report + 5, 38, state);
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
        esp_err_t err = dsp_model_read_drc(&state);
        if (err != ESP_OK) return err;
        return mvs_drc_a800x_to_view(&state, view);
    }
    if (path->drc_schema == MVS_DRC_SCHEMA_CLASSIC_3BAND) {
        mvs_drc_classic_state_t state;
        esp_err_t err = read_drc_classic_id(path->drc.effect_id, &state);
        if (err != ESP_OK) return err;
        return mvs_drc_classic_to_view(&state, view);
    }
    return ESP_ERR_NOT_SUPPORTED;
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
        mvs_drc_packed_state_t state;
        esp_err_t err = dsp_model_read_drc(&state);
        if (err != ESP_OK) return err;
        if (state.mode != 0) return ESP_ERR_INVALID_STATE;
        state.enabled = requested->enabled ? 1U : 0U;
        state.pregains[3] = (uint16_t)lround(4096.0 * pow(10.0, requested->pregain_db / 20.0));
        state.thresholds[3] = (int16_t)lround(requested->threshold_db * 100.0);
        state.ratios[3] = (uint16_t)lround(requested->ratio * 100.0);
        state.attacks[3] = requested->attack_ms;
        state.releases[3] = requested->release_ms;
        err = dsp_model_update_drc(&state);
        if (err != ESP_OK) return err;
        return dsp_model_read_drc_view(confirmed);
    }

    if (path->drc_schema == MVS_DRC_SCHEMA_CLASSIC_3BAND) {
        mvs_drc_classic_state_t before;
        esp_err_t err = read_drc_classic_id(effect_id, &before);
        if (err != ESP_OK) return err;
        if (before.mode != 2) return ESP_ERR_INVALID_STATE;
        mvs_drc_classic_state_t desired = before;
        desired.enabled = requested->enabled ? 1U : 0U;
        desired.thresholds[2] = (int16_t)lround(requested->threshold_db * 100.0);
        desired.ratios[2] = (uint16_t)lround(requested->ratio);
        desired.attacks[2] = requested->attack_ms;
        desired.releases[2] = requested->release_ms;
        desired.pregain1 = (uint16_t)lround(4096.0 * pow(10.0, requested->pregain_db / 20.0));

        uint16_t threshold_values[3], ratio_values[3], attack_values[3], release_values[3];
        for (size_t i = 0; i < 3; i++) {
            threshold_values[i] = (uint16_t)desired.thresholds[i];
            ratio_values[i] = desired.ratios[i];
            attack_values[i] = desired.attacks[i];
            release_values[i] = desired.releases[i];
        }
        err = send_u16_array(effect_id, 4, threshold_values, 3);
        if (err == ESP_OK) err = send_u16_array(effect_id, 5, ratio_values, 3);
        if (err == ESP_OK) err = send_u16_array(effect_id, 6, attack_values, 3);
        if (err == ESP_OK) err = send_u16_array(effect_id, 7, release_values, 3);
        if (err == ESP_OK) {
            uint8_t frame[8];
            err = mvs_build_write_frame(effect_id, 8, desired.pregain1, frame, sizeof(frame));
            if (err == ESP_OK) err = send_mvs_command(frame, sizeof(frame));
        }
        if (err == ESP_OK) {
            uint8_t frame[8];
            err = mvs_build_write_frame(effect_id, 0, desired.enabled, frame, sizeof(frame));
            if (err == ESP_OK) err = send_mvs_command(frame, sizeof(frame));
        }
        if (err != ESP_OK) return err;

        mvs_drc_classic_state_t after;
        err = read_drc_classic_id(effect_id, &after);
        if (err != ESP_OK) return err;
        if (after.fc != before.fc || after.mode != before.mode ||
            memcmp(after.q, before.q, sizeof(before.q)) != 0 ||
            after.pregain2 != before.pregain2 ||
            memcmp(after.thresholds, desired.thresholds, sizeof(desired.thresholds)) != 0 ||
            memcmp(after.ratios, desired.ratios, sizeof(desired.ratios)) != 0 ||
            memcmp(after.attacks, desired.attacks, sizeof(desired.attacks)) != 0 ||
            memcmp(after.releases, desired.releases, sizeof(desired.releases)) != 0 ||
            after.pregain1 != desired.pregain1 || after.enabled != desired.enabled)
            return ESP_ERR_INVALID_RESPONSE;
        return mvs_drc_classic_to_view(&after, confirmed);
    }
    return ESP_ERR_NOT_SUPPORTED;
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
    profile->drc.enabled = view->enabled ? 1U : 0U;
    profile->drc.mode = 0;
    profile->drc.pregains[3] = (uint16_t)lround(4096.0 * pow(10.0, view->pregain_db / 20.0));
    profile->drc.thresholds[3] = (int16_t)lround(view->threshold_db * 100.0);
    profile->drc.ratios[3] = (uint16_t)lround(view->ratio * 100.0);
    profile->drc.attacks[3] = view->attack_ms;
    profile->drc.releases[3] = view->release_ms;
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
        esp_err_t err = dsp_model_read_virtual_bass(&en, &cut, &it, &enh);
        if (err != ESP_OK) return err;
        if (en != expected->virtual_bass_enabled) return ESP_ERR_INVALID_RESPONSE;
        if (expected->virtual_bass_enabled &&
            (cut != expected->virtual_bass_cutoff_hz ||
             it != expected->virtual_bass_intensity_pct ||
             enh != expected->virtual_bass_enhanced))
            return ESP_ERR_INVALID_RESPONSE;
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
        if (memcmp(&state, &expected->preeq, sizeof(state)) != 0)
            return ESP_ERR_INVALID_RESPONSE;
    }

    // Out EQ
    if (dev->out_eq.available && expected->out_eq_valid) {
        mvs_preeq_state_t state;
        esp_err_t err = dsp_model_read_outeq_path(path_id, &state);
        if (err != ESP_OK) return err;
        if (memcmp(&state, &expected->out_eq, sizeof(state)) != 0)
            return ESP_ERR_INVALID_RESPONSE;
    }

    // DRC
    if (dev->drc.available) {
        dsp_drc_view_t view;
        esp_err_t err = dsp_model_read_drc_view_path(path_id, &view);
        if (err != ESP_OK) return err;
        dsp_drc_view_t exp;
        load_drc_view(expected, &exp);
        if (memcmp(&view, &exp, sizeof(view)) != 0)
            return ESP_ERR_INVALID_RESPONSE;
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
    if (report_len < 14) return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_delay(report + 5, report_len - 6, enable, delay_ms, hq_enabled);
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
    // Expect: header(5) + payload + terminator(1)
    if (report_len < 12) return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_usb_out_gain(report + 5, report_len - 6, gain_raw);
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
    if (report_len < 7) return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_phase(report + 5, report_len - 6, phase_invert);
}

esp_err_t dsp_model_set_phase(bool phase_invert)
{
    if (!s_device_profile.phase.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t phase_id = s_device_profile.phase.effect_id;
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(phase_id, 0, phase_invert ? 1 : 0,
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
    mvs_drc_packed_state_t state;
    esp_err_t err = dsp_model_read_drc(&state);
    if (err != ESP_OK) return err;
    state.enabled = enable ? 1 : 0;
    return dsp_model_update_drc(&state);
}

esp_err_t dsp_model_update_drc(const mvs_drc_packed_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.drc.available ||
        s_device_profile.drc_schema != MVS_DRC_SCHEMA_A800X_4PATH)
        return ESP_ERR_NOT_SUPPORTED;

    uint8_t drc_id = s_device_profile.drc.effect_id;
    uint8_t frame[60];

    esp_err_t err = mvs_build_drc_a800x_full_frame(drc_id, state,
                                                    frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, sizeof(frame));
}

static void load_drc_view(const dsp_profile_t *profile, dsp_drc_view_t *view)
{
    if (!profile || !view) return;
    memset(view, 0, sizeof(*view));
    view->valid = true;
    view->enabled = profile->drc.enabled != 0;
    view->full_band_supported = true;
    view->pregain_db = (profile->drc.pregains[3] / 4096.0) > 0.0
        ? 20.0 * log10(profile->drc.pregains[3] / 4096.0) : -72.0;
    view->threshold_db = profile->drc.thresholds[3] / 100.0;
    view->ratio = profile->drc.ratios[3] / 100.0;
    view->attack_ms = profile->drc.attacks[3];
    view->release_ms = profile->drc.releases[3];
}

static void store_drc_view(dsp_profile_t *profile, const dsp_drc_view_t *view)
{
    if (!profile || !view) return;
    profile->drc.enabled = view->enabled ? 1U : 0U;
    profile->drc.mode = 0;
    profile->drc.thresholds[3] = (int16_t)lround(view->threshold_db * 100.0);
    profile->drc.ratios[3] = (uint16_t)lround(view->ratio * 100.0);
    profile->drc.attacks[3] = view->attack_ms;
    profile->drc.releases[3] = view->release_ms;
    profile->drc.pregains[3] = (uint16_t)lround(4096.0 * pow(10.0, view->pregain_db / 20.0));
}

static inline uint16_t read_u16_le(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}
