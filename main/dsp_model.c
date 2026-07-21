// SPDX-FileCopyrightText: 2026 CobbyCode
// SPDX-License-Identifier: GPL-3.0-or-later
//
// dsp_model.c — DSP-Modell — Zustand und Parameter
//
// Alle Schreiboperationen verwenden dynamische Effekt-IDs aus dem
// aktiven Geräteprofil (mvs_device_profile.h).
//

#include "dsp_model.h"
#include "usb_host_ctrl.h"
#include "mvs_protocol.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
#include "esp_check.h"
#include <math.h>

static const char *TAG = "bp10_dsp";

// Internes Cache des DSP-Zustands
static dsp_profile_t s_current_profile;

// Aktives Geräteprofil (Effekt-IDs)
static mvs_device_profile_t s_device_profile = {0};

// ---------------------------------------------------------------------------
// HID-Transfer-Helper
// ---------------------------------------------------------------------------

static esp_err_t send_mvs_command(const uint8_t *frame, uint16_t frame_len)
{
    uint8_t report[256];
    mvs_prepare_hid_report(frame, frame_len, report);
    return usb_host_ctrl_send_report(report, sizeof(report));
}

static esp_err_t read_module_state(uint8_t effect_id, uint8_t *state,
                                   uint16_t state_capacity,
                                   uint16_t *state_len)
{
    if (!effect_id || !state || !state_len) return ESP_ERR_INVALID_ARG;
    uint8_t frame[5], report[256];
    uint16_t report_len = 0;
    ESP_RETURN_ON_ERROR(mvs_build_query_frame(effect_id, frame, sizeof(frame)),
                        TAG, "query frame");
    ESP_RETURN_ON_ERROR(send_mvs_command(frame, sizeof(frame)), TAG, "query send");
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_RETURN_ON_ERROR(usb_host_ctrl_get_report(report, &report_len), TAG,
                        "query read");
    if (report_len < 6 || report[0] != MVS_FRAME_MAGIC_1 ||
        report[1] != MVS_FRAME_MAGIC_2 || report[2] != effect_id ||
        report[3] < 1 || report[4] != 0xFF ||
        (size_t)report[3] + 5U > report_len ||
        report[4U + report[3]] != MVS_FRAME_TERMINATOR)
        return ESP_ERR_INVALID_RESPONSE;
    uint16_t length = (uint16_t)(report[3] - 1U);
    if (length > state_capacity) return ESP_ERR_INVALID_SIZE;
    memcpy(state, report + 5, length);
    *state_len = length;
    return ESP_OK;
}

static void store_drc_view(dsp_profile_t *profile, const dsp_drc_view_t *view)
{
    memset(&profile->drc, 0, sizeof(profile->drc));
    profile->drc.enabled = view->enabled ? 1U : 0U;
    profile->drc.mode = 0;
    profile->drc.pregains[3] = (uint16_t)lround(
        4096.0 * pow(10.0, view->pregain_db / 20.0));
    profile->drc.thresholds[3] = (int16_t)lround(view->threshold_db * 100.0);
    profile->drc.ratios[3] = (uint16_t)lround(view->ratio * 100.0);
    profile->drc.attacks[3] = view->attack_ms;
    profile->drc.releases[3] = view->release_ms;
}

static void load_drc_view(const dsp_profile_t *profile, dsp_drc_view_t *view)
{
    memset(view, 0, sizeof(*view));
    view->enabled = profile->drc.enabled != 0;
    view->full_band_supported = true;
    view->pregain_db = profile->drc.pregains[3] > 0
        ? 20.0 * log10(profile->drc.pregains[3] / 4096.0) : 0.0;
    view->threshold_db = profile->drc.thresholds[3] / 100.0;
    view->ratio = profile->drc.ratios[3] / 100.0;
    view->attack_ms = profile->drc.attacks[3];
    view->release_ms = profile->drc.releases[3];
}

// ---------------------------------------------------------------------------
// Öffentliche API — Profil
// ---------------------------------------------------------------------------

void dsp_model_set_device_profile(const mvs_device_profile_t *profile)
{
    if (!profile) return;
    s_device_profile = *profile;

    const char *kind_name = "unknown";
    switch (profile->kind) {
        case MVS_DEVICE_A800X_FIXED: kind_name = "A800X"; break;
        case MVS_DEVICE_GENERIC_ACP: kind_name = "Generic ACP"; break;
        default: break;
    }
    ESP_LOGI(TAG, "Geräteprofil gesetzt: %s (NS:0x%02X VB:0x%02X SD:0x%02X PEQ:0x%02X DRC:0x%02X)",
             kind_name,
             mvs_effect_id_ns(&s_device_profile),
             mvs_effect_id_vb(&s_device_profile),
             mvs_effect_id_sd(&s_device_profile),
             mvs_effect_id_preeq(&s_device_profile),
             mvs_effect_id_drc(&s_device_profile));
}

const mvs_device_profile_t *dsp_model_get_device_profile(void)
{
    return &s_device_profile;
}

uint8_t dsp_model_get_effect_id_ns(void)
{
    return mvs_effect_id_ns(&s_device_profile);
}
uint8_t dsp_model_get_effect_id_vb(void)
{
    return mvs_effect_id_vb(&s_device_profile);
}
uint8_t dsp_model_get_effect_id_sd(void)
{
    return mvs_effect_id_sd(&s_device_profile);
}
uint8_t dsp_model_get_effect_id_preeq(void)
{
    return mvs_effect_id_preeq(&s_device_profile);
}
uint8_t dsp_model_get_effect_id_drc(void)
{
    return mvs_effect_id_drc(&s_device_profile);
}
uint8_t dsp_model_get_effect_id_vb_classic(void)
{
    return mvs_effect_id_vb_classic(&s_device_profile);
}
uint8_t dsp_model_get_effect_id_phase(void)
{
    return mvs_effect_id_phase(&s_device_profile);
}
uint8_t dsp_model_get_effect_id_delay_hq(void)
{
    return mvs_effect_id_delay_hq(&s_device_profile);
}
uint8_t dsp_model_get_effect_id_usb_out_gain(void)
{
    return mvs_effect_id_usb_out_gain(&s_device_profile);
}

// ---------------------------------------------------------------------------
// Profil-Helper (Single-Module-Pattern)
// ---------------------------------------------------------------------------

void dsp_model_get_profile(dsp_profile_t *out)
{
    if (out) memcpy(out, &s_current_profile, sizeof(dsp_profile_t));
}

void dsp_model_commit_profile(const dsp_profile_t *profile)
{
    if (profile) memcpy(&s_current_profile, profile, sizeof(dsp_profile_t));
}

// ---------------------------------------------------------------------------
// Öffentliche API — Initialisierung
// ---------------------------------------------------------------------------

esp_err_t dsp_model_init(void)
{
    memset(&s_current_profile, 0, sizeof(s_current_profile));
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

    // Factory defaults derived from the AIYIMA A800X BP1048B2 implementation.

    // Noise Suppressor: Factory Defaults
    profile->noise_suppressor_enabled = true;
    profile->noise_suppressor_threshold_raw = -5500; // -55.00 dB
    profile->noise_suppressor_ratio = 4;
    profile->noise_suppressor_attack_ms = 2;
    profile->noise_suppressor_release_ms = 100;

    // Silence Detector: Hardware-Standard (DSP-chipseitig EIN)
    profile->silence_detector_enabled = true;

    // Virtual Bass: Standard
    profile->virtual_bass_enabled = true;
    profile->virtual_bass_cutoff_hz = 42;
    profile->virtual_bass_intensity_pct = 4;
    profile->virtual_bass_enhanced = true;

    // PreEQ: Factory Defaults
    profile->preeq.block_enabled = true;
    profile->preeq.pre_gain_raw = 0;
    profile->preeq.selected_filter = 0;

    // F0: LP 280 Hz, Q 1.2
    profile->preeq.filters[0].enabled = 1;
    profile->preeq.filters[0].type = MVS_FILTER_LP;
    profile->preeq.filters[0].frequency_hz = 280;
    profile->preeq.filters[0].q_raw = 1229;  // 1.2002 * 1024
    profile->preeq.filters[0].gain_raw = 0;

    // F1: LP 500 Hz, Q 0.707
    profile->preeq.filters[1].enabled = 1;
    profile->preeq.filters[1].type = MVS_FILTER_LP;
    profile->preeq.filters[1].frequency_hz = 500;
    profile->preeq.filters[1].q_raw = 724;  // 0.707 * 1024
    profile->preeq.filters[1].gain_raw = 0;

    // F2: HP 35 Hz, Q 0.8
    profile->preeq.filters[2].enabled = 1;
    profile->preeq.filters[2].type = MVS_FILTER_HP;
    profile->preeq.filters[2].frequency_hz = 35;
    profile->preeq.filters[2].q_raw = 819;  // 0.8 * 1024
    profile->preeq.filters[2].gain_raw = 0;

    // F3: PK 55 Hz, +1.5 dB, Q 3.5
    profile->preeq.filters[3].enabled = 1;
    profile->preeq.filters[3].type = MVS_FILTER_PK;
    profile->preeq.filters[3].frequency_hz = 55;
    profile->preeq.filters[3].q_raw = 3584;  // 3.5 * 1024
    profile->preeq.filters[3].gain_raw = 384;  // 1.5 dB * 256

    // F4: PK 85 Hz, +1.5 dB, Q 3.5
    profile->preeq.filters[4].enabled = 1;
    profile->preeq.filters[4].type = MVS_FILTER_PK;
    profile->preeq.filters[4].frequency_hz = 85;
    profile->preeq.filters[4].q_raw = 3584;
    profile->preeq.filters[4].gain_raw = 384;

    // F5-F9: disabled, neutral defaults (PK 20 kHz, 0 dB, Q 0.707)
    for (int i = 5; i < 10; i++) {
        profile->preeq.filters[i].enabled = 0;
        profile->preeq.filters[i].type = MVS_FILTER_PK;
        profile->preeq.filters[i].frequency_hz = 20000;
        profile->preeq.filters[i].q_raw = 724;  // 0.707 * 1024
        profile->preeq.filters[i].gain_raw = 0;
    }

    // DRC: Factory Full Band
    profile->drc.enabled = true;
    profile->drc.mode = 0;          // Full Band
    profile->drc.crossover_type = 1;
    profile->drc.crossover_q1_raw = 724;
    profile->drc.crossover_q2_raw = 724;
    profile->drc.crossover_freq1_hz = 300;
    profile->drc.crossover_freq2_hz = 2000;

    // Full Band: threshold -5 dB, ratio 1:1, attack 2 ms, release 800 ms
    for (int i = 0; i < 3; i++) {
        profile->drc.thresholds[i] = 0;
        profile->drc.ratios[i] = 100;     // 1.00:1
        profile->drc.attacks[i] = 2;
        profile->drc.releases[i] = 100;
        profile->drc.pregains[i] = 4096;  // 0 dB
    }
    profile->drc.thresholds[3] = -500;     // -5.00 dB
    profile->drc.ratios[3] = 100;          // 1.00:1
    profile->drc.attacks[3] = 2;
    profile->drc.releases[3] = 800;
    profile->drc.pregains[3] = 5157;       // ~+2 dB

    return true;
}

// ---------------------------------------------------------------------------
// dsp_model_apply_profile — vollständige Parametrierung
// ---------------------------------------------------------------------------

esp_err_t dsp_model_apply_profile(const dsp_profile_t *profile)
{
    if (!profile) return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.valid) {
        ESP_LOGW(TAG, "Kein Geräteprofil gesetzt – kann nicht anwenden");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Wende DSP-Profil an (Profil: %d)...", s_device_profile.kind);

    esp_err_t err;
    esp_err_t first_err = ESP_OK;

    // 1. Noise Suppressor (falls verfügbar)
    if (s_device_profile.noise_suppressor.available) {
        err = dsp_model_set_noise_suppressor_state(
            profile->noise_suppressor_enabled,
            profile->noise_suppressor_threshold_raw,
            profile->noise_suppressor_ratio,
            profile->noise_suppressor_attack_ms,
            profile->noise_suppressor_release_ms);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Noise Suppressor set fehlgeschlagen: %s", esp_err_to_name(err));
            if (first_err == ESP_OK) first_err = err;
        }
    } else {
        ESP_LOGD(TAG, "Noise Suppressor nicht verfügbar (übersprungen)");
    }

    // 2. Virtual Bass (falls verfügbar)
    if (s_device_profile.virtual_bass.available) {
        err = dsp_model_set_virtual_bass_state(
            profile->virtual_bass_enabled,
            profile->virtual_bass_cutoff_hz,
            profile->virtual_bass_intensity_pct,
            profile->virtual_bass_enhanced);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Virtual Bass set fehlgeschlagen: %s", esp_err_to_name(err));
            if (first_err == ESP_OK) first_err = err;
        }
    } else {
        ESP_LOGD(TAG, "Virtual Bass nicht verfügbar (übersprungen)");
    }

    // 3. Silence Detector (falls verfügbar)
    if (s_device_profile.silence_detector.available) {
        err = dsp_model_set_silence_detector(profile->silence_detector_enabled);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Silence Detector set fehlgeschlagen: %s", esp_err_to_name(err));
            if (first_err == ESP_OK) first_err = err;
        }
    } else {
        ESP_LOGD(TAG, "Silence Detector nicht verfügbar (übersprungen)");
    }

    // Old profiles already contained zeroed Phase-2 placeholders. Never turn
    // those zeros into DSP writes after an upgrade; only a fully confirmed
    // extended readback may arm persistence restore.
    if (profile->phase2_extended_valid &&
        s_device_profile.virtual_bass_classic.available) {
        err = dsp_model_set_virtual_bass_classic_state(
            profile->virtual_bass_classic_enabled,
            profile->virtual_bass_classic_cutoff_hz,
            profile->virtual_bass_classic_intensity_pct);
        if (err != ESP_OK && first_err == ESP_OK) first_err = err;
    }
    if (profile->phase2_extended_valid && s_device_profile.phase.available) {
        err = dsp_model_set_phase(profile->phase_invert);
        if (err != ESP_OK && first_err == ESP_OK) first_err = err;
    }
    if (profile->phase2_extended_valid && s_device_profile.delay_hq.available) {
        err = dsp_model_set_delay(profile->delay_enabled, profile->delay_ms,
                                  profile->delay_hq_enabled);
        if (err != ESP_OK && first_err == ESP_OK) first_err = err;
    }

    // 4. PreEQ (falls verfügbar)
    if (s_device_profile.preeq.available) {
        err = dsp_model_update_preeq(&profile->preeq);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "PreEQ-State set fehlgeschlagen: %s", esp_err_to_name(err));
            if (first_err == ESP_OK) first_err = err;
        }
    } else {
        ESP_LOGD(TAG, "PreEQ nicht verfügbar (übersprungen)");
    }

    // 5. DRC (falls verfügbar)
    if (s_device_profile.drc.available) {
        if (s_device_profile.drc_schema == MVS_DRC_SCHEMA_A800X_4PATH) {
            err = dsp_model_update_drc(&profile->drc);
        } else {
            dsp_drc_view_t requested, confirmed;
            load_drc_view(profile, &requested);
            err = dsp_model_update_drc_view(&requested, &confirmed);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "DRC-State set fehlgeschlagen: %s", esp_err_to_name(err));
            if (first_err == ESP_OK) first_err = err;
        }
    } else {
        ESP_LOGD(TAG, "DRC nicht verfügbar (übersprungen)");
    }

    if (first_err != ESP_OK) {
        ESP_LOGW(TAG, "DSP-Profil nur teilweise angewendet: %s",
                 esp_err_to_name(first_err));
        return first_err;
    }

    ESP_LOGI(TAG, "DSP-Profil angewendet (kein Flash-Save)");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Readback
// ---------------------------------------------------------------------------

esp_err_t dsp_model_readback(dsp_profile_t *profile)
{
    if (!profile) return ESP_ERR_INVALID_ARG;

    memset(profile, 0, sizeof(*profile));

    ESP_LOGI(TAG, "Lese DSP-Zustand aus...");

    uint8_t frame[16];
    uint8_t report[256];
    uint16_t report_len;

    // Noise Suppressor
    if (s_device_profile.noise_suppressor.available) {
        uint8_t ns_id = s_device_profile.noise_suppressor.effect_id;
        mvs_build_query_frame(ns_id, frame, sizeof(frame));
        mvs_prepare_hid_report(frame, 5, report);
        esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            err = usb_host_ctrl_get_report(report, &report_len);
            if (err == ESP_OK && report_len >= 16) {
                mvs_decode_noise_suppressor(report + 5, report_len - 5,
                                            &profile->noise_suppressor_enabled,
                                            &profile->noise_suppressor_threshold_raw,
                                            &profile->noise_suppressor_ratio,
                                            &profile->noise_suppressor_attack_ms,
                                            &profile->noise_suppressor_release_ms);
            }
        }
    }

    // Virtual Bass
    if (s_device_profile.virtual_bass.available) {
        uint8_t vb_id = s_device_profile.virtual_bass.effect_id;
        mvs_build_query_frame(vb_id, frame, sizeof(frame));
        mvs_prepare_hid_report(frame, 5, report);
        esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            err = usb_host_ctrl_get_report(report, &report_len);
            if (err == ESP_OK && report_len >= 10) {
                mvs_decode_virtual_bass(report + 5, report_len - 5,
                                        &profile->virtual_bass_enabled,
                                        &profile->virtual_bass_cutoff_hz,
                                        &profile->virtual_bass_intensity_pct,
                                        &profile->virtual_bass_enhanced);
            }
        }
    }

    // Silence Detector
    if (s_device_profile.silence_detector.available) {
        uint16_t silence_amplitude = 0;
        esp_err_t err = dsp_model_read_silence_detector(
            &profile->silence_detector_enabled, &silence_amplitude);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Silence-Readback fehlgeschlagen: %s", esp_err_to_name(err));
        }
    }

    bool extended_present = false;
    bool extended_ok = true;
    if (s_device_profile.virtual_bass_classic.available) {
        extended_present = true;
        esp_err_t err = dsp_model_read_virtual_bass_classic(
            &profile->virtual_bass_classic_enabled,
            &profile->virtual_bass_classic_cutoff_hz,
            &profile->virtual_bass_classic_intensity_pct);
        if (err != ESP_OK) { extended_ok = false; ESP_LOGW(TAG, "VB Classic readback failed: %s",
                                    esp_err_to_name(err)); }
    }
    if (s_device_profile.phase.available) {
        extended_present = true;
        esp_err_t err = dsp_model_read_phase(&profile->phase_invert);
        if (err != ESP_OK) { extended_ok = false; ESP_LOGW(TAG, "Phase readback failed: %s",
                                    esp_err_to_name(err)); }
    }
    if (s_device_profile.delay_hq.available) {
        extended_present = true;
        esp_err_t err = dsp_model_read_delay(&profile->delay_enabled, &profile->delay_ms,
                                             &profile->delay_hq_enabled);
        if (err != ESP_OK) { extended_ok = false; ESP_LOGW(TAG, "Delay readback failed: %s",
                                    esp_err_to_name(err)); }
    }
    profile->phase2_extended_valid = extended_present && extended_ok;

    // PreEQ (vollständiger, validierter Zustand)
    if (s_device_profile.preeq.available) {
        esp_err_t err = dsp_model_read_preeq(&profile->preeq);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "PreEQ-Readback fehlgeschlagen: %s", esp_err_to_name(err));
        }
    }

    // DRC (vollständiger, validierter Zustand)
    if (s_device_profile.drc.available) {
        esp_err_t err;
        if (s_device_profile.drc_schema == MVS_DRC_SCHEMA_A800X_4PATH) {
            err = dsp_model_read_drc(&profile->drc);
        } else {
            dsp_drc_view_t view;
            err = dsp_model_read_drc_view(&view);
            if (err == ESP_OK) store_drc_view(profile, &view);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "DRC-Readback fehlgeschlagen: %s", esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "DSP-Readback abgeschlossen");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// PreEQ Readback
// ---------------------------------------------------------------------------

esp_err_t dsp_model_read_preeq(mvs_preeq_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.preeq.available) return ESP_ERR_NOT_SUPPORTED;

    uint8_t peq_id = s_device_profile.preeq.effect_id;
    uint8_t frame[5];
    uint8_t report[256];
    uint16_t report_len = 0;

    mvs_build_query_frame(peq_id, frame, sizeof(frame));
    ESP_LOGI(TAG, "PreEQ 0x%02X TX: A5 5A %02X 00 16", peq_id, peq_id);
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 112 || report[0] != MVS_FRAME_MAGIC_1 ||
        report[1] != MVS_FRAME_MAGIC_2 || report[2] != peq_id ||
        report[4] != 0xFF) {
        ESP_LOGW(TAG, "Ungültiger PreEQ-Readback (len=%u)", report_len);
        return ESP_ERR_INVALID_RESPONSE;
    }
    err = mvs_decode_preeq(report + 5, report_len - 5, state);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "PreEQ: enabled=%u pregain_raw=%d selected=%u",
                 state->block_enabled, state->pre_gain_raw, state->selected_filter);
    }
    return err;
}

// ---------------------------------------------------------------------------
// DRC Readback
// ---------------------------------------------------------------------------

esp_err_t dsp_model_read_drc(mvs_drc_packed_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.drc.available ||
        s_device_profile.drc_schema != MVS_DRC_SCHEMA_A800X_4PATH)
        return ESP_ERR_NOT_SUPPORTED;

    uint8_t drc_id = s_device_profile.drc.effect_id;
    uint8_t frame[5];
    uint8_t report[256];
    uint16_t report_len = 0;

    esp_err_t err = mvs_build_query_frame(drc_id, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "DRC 0x%02X TX: A5 5A %02X 00 16", drc_id, drc_id);
    mvs_prepare_hid_report(frame, sizeof(frame), report);
    err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 60 || report[0] != MVS_FRAME_MAGIC_1 ||
        report[1] != MVS_FRAME_MAGIC_2 || report[2] != drc_id ||
        report[4] != 0xFF || report[59] != MVS_FRAME_TERMINATOR) {
        ESP_LOGW(TAG, "DRC 0x%02X ungültig (len=%u)", drc_id, report_len);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return mvs_decode_drc_a800x(report + 5, report_len - 5, state);
}

static esp_err_t read_drc_classic(mvs_drc_classic_state_t *state)
{
    if (!state || !s_device_profile.drc.available ||
        s_device_profile.drc_schema != MVS_DRC_SCHEMA_CLASSIC_3BAND)
        return ESP_ERR_NOT_SUPPORTED;
    uint8_t effect_id = s_device_profile.drc.effect_id;
    uint8_t frame[5], report[256];
    uint16_t report_len = 0;
    esp_err_t err = mvs_build_query_frame(effect_id, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    mvs_prepare_hid_report(frame, sizeof(frame), report);
    err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 44 || report[0] != MVS_FRAME_MAGIC_1 ||
        report[1] != MVS_FRAME_MAGIC_2 || report[2] != effect_id ||
        report[3] != 39 || report[4] != 0xFF || report[43] != MVS_FRAME_TERMINATOR)
        return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_drc_classic(report + 5, 38, state);
}

esp_err_t dsp_model_read_drc_view(dsp_drc_view_t *view)
{
    if (!view) return ESP_ERR_INVALID_ARG;
    if (s_device_profile.drc_schema == MVS_DRC_SCHEMA_A800X_4PATH) {
        mvs_drc_packed_state_t state;
        esp_err_t err = dsp_model_read_drc(&state);
        if (err != ESP_OK) return err;
        return mvs_drc_a800x_to_view(&state, view);
    }
    if (s_device_profile.drc_schema == MVS_DRC_SCHEMA_CLASSIC_3BAND) {
        mvs_drc_classic_state_t state;
        esp_err_t err = read_drc_classic(&state);
        if (err != ESP_OK) return err;
        return mvs_drc_classic_to_view(&state, view);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t send_u16_array(uint8_t effect_id, uint8_t selector,
                                const uint16_t *values, size_t count)
{
    uint8_t frame[16];
    size_t frame_len = 0;
    esp_err_t err = mvs_build_write_u16_array_frame(effect_id, selector, values,
                                                     count, frame, sizeof(frame),
                                                     &frame_len);
    if (err != ESP_OK) return err;
    err = send_mvs_command(frame, (uint16_t)frame_len);
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(20));
    return err;
}

esp_err_t dsp_model_update_drc_view(const dsp_drc_view_t *requested,
                                    dsp_drc_view_t *confirmed)
{
    if (!requested || !confirmed) return ESP_ERR_INVALID_ARG;
    uint8_t effect_id = s_device_profile.drc.effect_id;
    if (!s_device_profile.drc.available) return ESP_ERR_NOT_SUPPORTED;

    if (s_device_profile.drc_schema == MVS_DRC_SCHEMA_A800X_4PATH) {
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

    if (s_device_profile.drc_schema == MVS_DRC_SCHEMA_CLASSIC_3BAND) {
        mvs_drc_classic_state_t before;
        esp_err_t err = read_drc_classic(&before);
        if (err != ESP_OK) return err;
        if (before.mode != 2) return ESP_ERR_INVALID_STATE;
        mvs_drc_classic_state_t desired = before;
        desired.enabled = requested->enabled ? 1U : 0U;
        desired.thresholds[2] = (int16_t)lround(requested->threshold_db * 100.0);
        desired.ratios[2] = (uint16_t)lround(requested->ratio);
        desired.attacks[2] = requested->attack_ms;
        desired.releases[2] = requested->release_ms;
        desired.pregain1 = (uint16_t)lround(4096.0 * pow(10.0, requested->pregain_db / 20.0));

        uint16_t threshold_values[3];
        uint16_t ratio_values[3];
        uint16_t attack_values[3];
        uint16_t release_values[3];
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
            err = mvs_build_write_frame(effect_id, 8, desired.pregain1,
                                        frame, sizeof(frame));
            if (err == ESP_OK) err = send_mvs_command(frame, sizeof(frame));
        }
        if (err == ESP_OK) {
            uint8_t frame[8];
            err = mvs_build_write_frame(effect_id, 0, desired.enabled,
                                        frame, sizeof(frame));
            if (err == ESP_OK) err = send_mvs_command(frame, sizeof(frame));
        }
        if (err != ESP_OK) return err;

        mvs_drc_classic_state_t after;
        err = read_drc_classic(&after);
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
// Targeted Read: Noise Suppressor
// ---------------------------------------------------------------------------

esp_err_t dsp_model_read_noise_suppressor(bool *enabled, int16_t *threshold_raw,
                                           uint16_t *ratio, uint16_t *attack_ms,
                                           uint16_t *release_ms)
{
    if (!enabled || !threshold_raw || !ratio || !attack_ms || !release_ms)
        return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.noise_suppressor.available)
        return ESP_ERR_NOT_SUPPORTED;

    uint8_t ns_id = s_device_profile.noise_suppressor.effect_id;
    uint8_t frame[16];
    uint8_t report[256];
    uint16_t report_len = 0;

    mvs_build_query_frame(ns_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 16) return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_noise_suppressor(report + 5, report_len - 5,
                                       enabled, threshold_raw, ratio,
                                       attack_ms, release_ms);
}

// ---------------------------------------------------------------------------
// Targeted Read: Virtual Bass
// ---------------------------------------------------------------------------

esp_err_t dsp_model_read_virtual_bass(bool *enabled, uint16_t *cutoff_hz,
                                       uint16_t *intensity_pct, bool *enhanced)
{
    if (!enabled || !cutoff_hz || !intensity_pct || !enhanced)
        return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.virtual_bass.available)
        return ESP_ERR_NOT_SUPPORTED;

    uint8_t vb_id = s_device_profile.virtual_bass.effect_id;
    uint8_t frame[16];
    uint8_t report[256];
    uint16_t report_len = 0;

    mvs_build_query_frame(vb_id, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 10) return ESP_ERR_INVALID_RESPONSE;
    return mvs_decode_virtual_bass(report + 5, report_len - 5,
                                   enabled, cutoff_hz, intensity_pct, enhanced);
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
    profile->drc.pregains[3] = (uint16_t)lround(
        4096.0 * pow(10.0, view->pregain_db / 20.0));
    profile->drc.thresholds[3] = (int16_t)lround(view->threshold_db * 100.0);
    profile->drc.ratios[3] = (uint16_t)lround(view->ratio * 100.0);
    profile->drc.attacks[3] = view->attack_ms;
    profile->drc.releases[3] = view->release_ms;
}

// ---------------------------------------------------------------------------
// Full-Profile Verification (targeted reads, kein globaler Readback)
// ---------------------------------------------------------------------------

esp_err_t dsp_model_verify_full_profile(const dsp_profile_t *expected)
{
    if (!expected) return ESP_ERR_INVALID_ARG;
    const mvs_device_profile_t *dev = &s_device_profile;

    // Noise Suppressor
    if (dev->noise_suppressor.available) {
        bool en; int16_t thr; uint16_t rat, atk, rel;
        esp_err_t err = dsp_model_read_noise_suppressor(&en, &thr, &rat, &atk, &rel);
        if (err != ESP_OK) { ESP_LOGW(TAG, "verify: Noise read failed"); return err; }
        if (en != expected->noise_suppressor_enabled) {
            ESP_LOGW(TAG, "verify: Noise enabled mismatch exp=%d got=%d",
                     expected->noise_suppressor_enabled, en);
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (expected->noise_suppressor_enabled) {
            if (thr != expected->noise_suppressor_threshold_raw ||
                rat != expected->noise_suppressor_ratio ||
                atk != expected->noise_suppressor_attack_ms ||
                rel != expected->noise_suppressor_release_ms) {
                ESP_LOGW(TAG, "verify: Noise params mismatch");
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
    }

    // Virtual Bass
    if (dev->virtual_bass.available) {
        bool en, enh; uint16_t cut, it;
        esp_err_t err = dsp_model_read_virtual_bass(&en, &cut, &it, &enh);
        if (err != ESP_OK) { ESP_LOGW(TAG, "verify: VB read failed"); return err; }
        if (en != expected->virtual_bass_enabled) {
            ESP_LOGW(TAG, "verify: VB enabled mismatch");
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (expected->virtual_bass_enabled) {
            if (cut != expected->virtual_bass_cutoff_hz ||
                it != expected->virtual_bass_intensity_pct ||
                enh != expected->virtual_bass_enhanced) {
                ESP_LOGW(TAG, "verify: VB params mismatch");
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
    }

    // Virtual Bass Classic
    if (dev->virtual_bass_classic.available) {
        bool en; uint16_t cut, it;
        esp_err_t err = dsp_model_read_virtual_bass_classic(&en, &cut, &it);
        if (err != ESP_OK) { ESP_LOGW(TAG, "verify: VBC read failed"); return err; }
        if (en != expected->virtual_bass_classic_enabled) {
            ESP_LOGW(TAG, "verify: VBC enabled mismatch");
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (expected->virtual_bass_classic_enabled) {
            if (cut != expected->virtual_bass_classic_cutoff_hz ||
                it != expected->virtual_bass_classic_intensity_pct) {
                ESP_LOGW(TAG, "verify: VBC params mismatch");
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
    }

    // Music Phase
    if (dev->phase.available) {
        bool in;
        esp_err_t err = dsp_model_read_phase(&in);
        if (err != ESP_OK) { ESP_LOGW(TAG, "verify: Phase read failed"); return err; }
        if (in != expected->phase_invert) {
            ESP_LOGW(TAG, "verify: Phase mismatch");
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    // Music Delay
    if (dev->delay_hq.available) {
        bool en, hq; uint16_t ms;
        esp_err_t err = dsp_model_read_delay(&en, &ms, &hq);
        if (err != ESP_OK) { ESP_LOGW(TAG, "verify: Delay read failed"); return err; }
        if (en != expected->delay_enabled ||
            ms != expected->delay_ms ||
            hq != expected->delay_hq_enabled) {
            ESP_LOGW(TAG, "verify: Delay mismatch");
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    // Silence Detector
    if (dev->silence_detector.available) {
        bool en; uint16_t amp;
        esp_err_t err = dsp_model_read_silence_detector(&en, &amp);
        if (err != ESP_OK) { ESP_LOGW(TAG, "verify: Silence read failed"); return err; }
        if (en != expected->silence_detector_enabled) {
            ESP_LOGW(TAG, "verify: Silence enabled mismatch");
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    // PreEQ
    if (dev->preeq.available) {
        mvs_preeq_state_t state;
        esp_err_t err = dsp_model_read_preeq(&state);
        if (err != ESP_OK) { ESP_LOGW(TAG, "verify: PreEQ read failed"); return err; }
        // Apply same normalization as dsp_model_update_preeq
        for (int i = 0; i < 10; i++) {
            mvs_preeq_filter_t *f = &state.filters[i];
            if (!f->enabled && f->frequency_hz == 0 && f->q_raw == 0) {
                f->type = MVS_FILTER_PK;
                f->frequency_hz = 20000;
                f->q_raw = 724;
                f->gain_raw = 0;
            }
        }
        mvs_prepare_preeq_for_schema(dev->preeq_schema, &state);
        if (memcmp(&state, &expected->preeq, sizeof(state)) != 0) {
            ESP_LOGW(TAG, "verify: PreEQ mismatch");
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    // DRC
    if (dev->drc.available) {
        dsp_drc_view_t view;
        esp_err_t err = dsp_model_read_drc_view(&view);
        if (err != ESP_OK) { ESP_LOGW(TAG, "verify: DRC read failed"); return err; }
        if (view.enabled != (expected->drc.enabled != 0)) {
            ESP_LOGW(TAG, "verify: DRC enabled mismatch");
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (view.enabled) {
            dsp_drc_view_t exp_view;
            if (dev->drc_schema == MVS_DRC_SCHEMA_A800X_4PATH) {
                mvs_drc_a800x_to_view((const mvs_drc_packed_state_t *)&expected->drc, &exp_view);
            } else {
                exp_view = view; /* schema mismatch: skip detailed comparison */
            }
            if (dev->drc_schema == MVS_DRC_SCHEMA_A800X_4PATH &&
                (fabs(exp_view.pregain_db - view.pregain_db) > 0.01 ||
                 fabs(exp_view.threshold_db - view.threshold_db) > 0.02 ||
                 fabs(exp_view.ratio - view.ratio) > 0.02 ||
                 exp_view.attack_ms != view.attack_ms ||
                 exp_view.release_ms != view.release_ms)) {
                ESP_LOGW(TAG, "verify: DRC params mismatch");
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
    }

    ESP_LOGI(TAG, "Full-Profile verification passed");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Silence Detector Readback
// ---------------------------------------------------------------------------

esp_err_t dsp_model_read_silence_detector(bool *enabled, uint16_t *amplitude)
{
    if (!enabled || !amplitude) return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.silence_detector.available) return ESP_ERR_NOT_SUPPORTED;

    uint8_t sd_id = s_device_profile.silence_detector.effect_id;
    uint8_t frame[5];
    uint8_t report[256];
    uint16_t report_len = 0;
    esp_err_t err = mvs_build_query_frame(sd_id, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    mvs_prepare_hid_report(frame, sizeof(frame), report);
    err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(100));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 10 || report[0] != MVS_FRAME_MAGIC_1 ||
        report[1] != MVS_FRAME_MAGIC_2 ||
        report[2] != sd_id || report[4] != 0xFF) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *enabled = ((uint16_t)report[5] | ((uint16_t)report[6] << 8)) != 0;
    *amplitude = (uint16_t)report[7] | ((uint16_t)report[8] << 8);
    return ESP_OK;
}

esp_err_t dsp_model_read_effect_enabled(uint8_t effect_id, bool *enabled)
{
    if (!enabled) return ESP_ERR_INVALID_ARG;

    uint8_t frame[5];
    uint8_t report[256];
    uint16_t report_len = 0;

    esp_err_t err = mvs_build_query_frame(effect_id, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    mvs_prepare_hid_report(frame, sizeof(frame), report);
    err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(100));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    if (report_len < 7 || report[0] != MVS_FRAME_MAGIC_1 ||
        report[1] != MVS_FRAME_MAGIC_2 || report[2] != effect_id ||
        report[4] != 0xFF) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *enabled = ((uint16_t)report[5] | ((uint16_t)report[6] << 8)) != 0;
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Noise Suppressor
// ---------------------------------------------------------------------------

esp_err_t dsp_model_set_noise_suppressor(bool enable)
{
    if (!s_device_profile.noise_suppressor.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t ns_id = s_device_profile.noise_suppressor.effect_id;

    ESP_LOGI(TAG, "Noise Suppressor 0x%02X %s", ns_id, enable ? "EIN" : "AUS");
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(ns_id, MVS_SEL_BLOCK_ENABLE,
                                          enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 8);
}

esp_err_t dsp_model_set_noise_suppressor_state(bool enable,
                                                int16_t threshold_raw,
                                                uint16_t ratio,
                                                uint16_t attack_ms,
                                                uint16_t release_ms)
{
    if (!s_device_profile.noise_suppressor.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t ns_id = s_device_profile.noise_suppressor.effect_id;

    // The DSP rejects parameter writes while Noise Suppressor is disabled.
    if (!enable) return dsp_model_set_noise_suppressor(false);

    const uint16_t values[] = {
        1U,
        (uint16_t)threshold_raw,
        ratio,
        attack_ms,
        release_ms,
    };

    for (uint8_t selector = MVS_SEL_BLOCK_ENABLE;
         selector <= MVS_SEL_PARAM_4; selector++) {
        uint8_t frame[8];
        esp_err_t err = mvs_build_write_frame(ns_id, selector, values[selector],
                                              frame, sizeof(frame));
        if (err != ESP_OK) return err;
        err = send_mvs_command(frame, sizeof(frame));
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Virtual Bass
// ---------------------------------------------------------------------------

esp_err_t dsp_model_set_virtual_bass(bool enable)
{
    if (!s_device_profile.virtual_bass.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t vb_id = s_device_profile.virtual_bass.effect_id;

    ESP_LOGI(TAG, "Virtual Bass 0x%02X %s", vb_id, enable ? "EIN" : "AUS");
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(vb_id, MVS_SEL_BLOCK_ENABLE,
                                          enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 8);
}

esp_err_t dsp_model_set_virtual_bass_state(bool enable, uint16_t cutoff_hz,
                                            uint16_t intensity_pct,
                                            bool bass_enhanced)
{
    if (!s_device_profile.virtual_bass.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t vb_id = s_device_profile.virtual_bass.effect_id;

    // Disabling is a single command. Parameter writes while disabled
    // can cause the DSP to reject the sequence.
    if (!enable) return dsp_model_set_virtual_bass(false);

    const uint8_t selectors[] = {
        MVS_SEL_BLOCK_ENABLE,
        MVS_SEL_PARAM_1, MVS_SEL_PARAM_2, MVS_SEL_PARAM_3,
    };
    const uint16_t values[] = {
        1U, cutoff_hz, intensity_pct, bass_enhanced ? 1U : 0U,
    };
    for (size_t i = 0; i < sizeof(selectors) / sizeof(selectors[0]); i++) {
        uint8_t frame[8];
        esp_err_t err = mvs_build_write_frame(vb_id, selectors[i], values[i],
                                              frame, sizeof(frame));
        if (err != ESP_OK) return err;
        err = send_mvs_command(frame, sizeof(frame));
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

esp_err_t dsp_model_read_virtual_bass_classic(bool *enable, uint16_t *cutoff_hz,
                                               uint16_t *intensity_pct)
{
    if (!s_device_profile.virtual_bass_classic.available)
        return ESP_ERR_NOT_SUPPORTED;
    uint8_t state[10]; uint16_t length = 0;
    ESP_RETURN_ON_ERROR(read_module_state(
        s_device_profile.virtual_bass_classic.effect_id, state, sizeof(state),
        &length), TAG, "VB Classic read");
    return mvs_decode_virtual_bass_classic(state, length, enable, cutoff_hz,
                                           intensity_pct);
}

esp_err_t dsp_model_set_virtual_bass_classic_state(bool enable,
                                                    uint16_t cutoff_hz,
                                                    uint16_t intensity_pct)
{
    if (!s_device_profile.virtual_bass_classic.available)
        return ESP_ERR_NOT_SUPPORTED;
    uint8_t id = s_device_profile.virtual_bass_classic.effect_id;
    const uint16_t values[] = { enable ? 1U : 0U, cutoff_hz, intensity_pct };
    size_t count = enable ? 3U : 1U;
    for (size_t selector = 0; selector < count; selector++) {
        uint8_t frame[8];
        ESP_RETURN_ON_ERROR(mvs_build_write_frame(id, (uint8_t)selector,
                            values[selector], frame, sizeof(frame)), TAG,
                            "VB Classic frame");
        ESP_RETURN_ON_ERROR(send_mvs_command(frame, sizeof(frame)), TAG,
                            "VB Classic send");
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

esp_err_t dsp_model_read_phase(bool *phase_invert)
{
    if (!phase_invert) return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.phase.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t state[4]; uint16_t length = 0;
    ESP_RETURN_ON_ERROR(read_module_state(s_device_profile.phase.effect_id,
                        state, sizeof(state), &length), TAG, "Phase read");
    return mvs_decode_phase(state, length, phase_invert);
}

esp_err_t dsp_model_set_phase(bool phase_invert)
{
    if (!s_device_profile.phase.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t selector = s_device_profile.phase.state_size == 4 ? 1U : 0U;
    uint8_t frame[8];
    ESP_RETURN_ON_ERROR(mvs_build_write_frame(s_device_profile.phase.effect_id,
                        selector, phase_invert ? 1U : 0U, frame, sizeof(frame)),
                        TAG, "Phase frame");
    ESP_RETURN_ON_ERROR(send_mvs_command(frame, sizeof(frame)), TAG, "Phase send");
    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}

esp_err_t dsp_model_read_delay(bool *enable, uint16_t *delay_ms,
                               bool *hq_enabled)
{
    if (!enable || !delay_ms || !hq_enabled) return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.delay_hq.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t state[10]; uint16_t length = 0;
    ESP_RETURN_ON_ERROR(read_module_state(s_device_profile.delay_hq.effect_id,
                        state, sizeof(state), &length), TAG, "Delay read");
    return mvs_decode_delay(state, length, enable, delay_ms, hq_enabled);
}

esp_err_t dsp_model_set_delay(bool enable, uint16_t delay_ms,
                              bool hq_enabled)
{
    if (!s_device_profile.delay_hq.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t id = s_device_profile.delay_hq.effect_id;
    // The confirmed Classic module rejects parameter writes while disabled.
    // Enable temporarily, update both channels and HQ, then leave the block in
    // the requested final state.
    const uint16_t values[] = { 1U, delay_ms, delay_ms,
                                hq_enabled ? 1U : 0U, enable ? 1U : 0U };
    const uint8_t selectors[] = { 0U, 1U, 2U, 3U, 0U };
    for (size_t i = 0; i < sizeof(selectors); i++) {
        uint8_t frame[8];
        ESP_RETURN_ON_ERROR(mvs_build_write_frame(id, selectors[i], values[i],
                            frame, sizeof(frame)), TAG, "Delay frame");
        ESP_RETURN_ON_ERROR(send_mvs_command(frame, sizeof(frame)), TAG,
                            "Delay send");
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Silence Detector
// ---------------------------------------------------------------------------

esp_err_t dsp_model_set_silence_detector(bool enable)
{
    if (!s_device_profile.silence_detector.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t sd_id = s_device_profile.silence_detector.effect_id;

    ESP_LOGI(TAG, "Silence Detector 0x%02X %s", sd_id, enable ? "EIN" : "AUS");
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(sd_id, MVS_SEL_BLOCK_ENABLE,
                                          enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 8);
}

// ---------------------------------------------------------------------------
// PreEQ
// ---------------------------------------------------------------------------

esp_err_t dsp_model_set_preeq_enable(bool enable)
{
    if (!s_device_profile.preeq.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t peq_id = s_device_profile.preeq.effect_id;

    ESP_LOGI(TAG, "PreEQ 0x%02X %s", peq_id, enable ? "EIN" : "AUS");
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(peq_id, MVS_SEL_BLOCK_ENABLE,
                                          enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 8);
}

esp_err_t dsp_model_update_preeq(const mvs_preeq_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.preeq.available) return ESP_ERR_NOT_SUPPORTED;

    uint8_t peq_id = s_device_profile.preeq.effect_id;

    // Defensiver Schutz: Korrumpierte deaktivierte Filter reparieren.
    mvs_preeq_state_t normalized = *state;
    int repaired = 0;
    for (int i = 0; i < 10; i++) {
        mvs_preeq_filter_t *f = &normalized.filters[i];
        if (!f->enabled && f->frequency_hz == 0 && f->q_raw == 0) {
            f->type = MVS_FILTER_PK;
            f->frequency_hz = 20000;
            f->q_raw = 724;
            f->gain_raw = 0;
            repaired++;
        }
    }
    if (repaired > 0) {
        ESP_LOGI(TAG, "PreEQ: repaired %d corrupted disabled filters", repaired);
    }

    // Schema-Adapter anwenden
    mvs_prepare_preeq_for_schema(s_device_profile.preeq_schema, &normalized);

    ESP_LOGI(TAG, "PreEQ 0x%02X State-Update", peq_id);
    uint8_t frame[128];
    esp_err_t err = mvs_build_preeq_full_frame_dyn(peq_id, &normalized, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 112);
}

// ---------------------------------------------------------------------------
// DRC
// ---------------------------------------------------------------------------

esp_err_t dsp_model_set_drc_enable(bool enable)
{
    if (!s_device_profile.drc.available) return ESP_ERR_NOT_SUPPORTED;
    uint8_t drc_id = s_device_profile.drc.effect_id;

    ESP_LOGI(TAG, "DRC 0x%02X %s", drc_id, enable ? "EIN" : "AUS");
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(drc_id, MVS_SEL_BLOCK_ENABLE,
                                          enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 8);
}

esp_err_t dsp_model_update_drc(const mvs_drc_packed_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    if (!s_device_profile.drc.available ||
        s_device_profile.drc_schema != MVS_DRC_SCHEMA_A800X_4PATH)
        return ESP_ERR_NOT_SUPPORTED;

    uint8_t drc_id = s_device_profile.drc.effect_id;

    ESP_LOGI(TAG, "DRC 0x%02X State-Update (A800X 4-Pfad)", drc_id);
    uint8_t frame[64];
    esp_err_t err = mvs_build_drc_a800x_full_frame(drc_id, state, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 60);
}
