// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// dsp_model.c — DSP-Modell — Zustand und Parameter
//

#include "dsp_model.h"
#include "usb_host_ctrl.h"
#include "mvs_protocol.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "a800x_dsp";

// Internes Cache des DSP-Zustands
static dsp_profile_t s_current_profile;

// ---------------------------------------------------------------------------
// HID-Transfer-Helper
// ---------------------------------------------------------------------------

static esp_err_t send_mvs_command(const uint8_t *frame, uint16_t frame_len)
{
    uint8_t report[256];
    mvs_prepare_hid_report(frame, frame_len, report);
    return usb_host_ctrl_send_report(report, sizeof(report));
}

// send_mvs_simple — reserved for future parameter writes
/*
static esp_err_t send_mvs_simple(uint8_t effect_id, uint8_t cmd_len,
                                  const uint8_t *payload, uint16_t payload_len)
{
    uint8_t frame[64];
    frame[0] = MVS_FRAME_MAGIC_1;
    frame[1] = MVS_FRAME_MAGIC_2;
    frame[2] = effect_id;
    frame[3] = cmd_len;
    if (payload && payload_len > 0) {
        memcpy(&frame[4], payload, payload_len);
    }
    frame[4 + payload_len] = MVS_FRAME_TERMINATOR;
    return send_mvs_command(frame, 5 + payload_len);
}
*/

// ---------------------------------------------------------------------------
// Öffentliche API
// ---------------------------------------------------------------------------

esp_err_t dsp_model_init(void)
{
    memset(&s_current_profile, 0, sizeof(s_current_profile));
    dsp_model_get_default_profile(&s_current_profile);
    return ESP_OK;
}

void dsp_model_get_default_profile(dsp_profile_t *profile)
{
    memset(profile, 0, sizeof(dsp_profile_t));
    strcpy(profile->profile_name, "default");

    // Noise Suppressor: Factory Defaults
    profile->noise_suppressor_enabled = true;
    profile->noise_suppressor_threshold_raw = -5500; // -55.00 dB
    profile->noise_suppressor_ratio = 4;
    profile->noise_suppressor_attack_ms = 2;
    profile->noise_suppressor_release_ms = 100;

    // Silence Detector: aus (Standard)
    profile->silence_detector_enabled = false;

    // Virtual Bass: Standard
    profile->virtual_bass_enabled = false;  // Aus für Workaround
    profile->virtual_bass_cutoff_hz = 42;
    profile->virtual_bass_intensity_pct = 4;
    profile->virtual_bass_enhanced = false;

    // PreEQ: Factory Defaults (siehe Command Reference)
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

    // F5-F9: disabled/zero
    // F7: disabled but has default 20 kHz, Q 0.707
    profile->preeq.filters[7].enabled = 0;
    profile->preeq.filters[7].type = MVS_FILTER_PK;
    profile->preeq.filters[7].frequency_hz = 20000;
    profile->preeq.filters[7].q_raw = 724;

    // DRC: Factory Full Band
    profile->drc.enabled = true;
    profile->drc.mode = 0;          // Full Band
    profile->drc.crossover_type = 1;
    profile->drc.crossover_q1_raw = 724;
    profile->drc.crossover_q2_raw = 724;
    profile->drc.crossover_freq1_hz = 300;
    profile->drc.crossover_freq2_hz = 2000;

    // Full Band: threshold -5 dB, ratio 1:1, attack 2 ms, release 800 ms
    // Inaktive Bänder auf Standard
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
}

esp_err_t dsp_model_apply_profile(const dsp_profile_t *profile)
{
    if (!profile) return ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "Wende DSP-Profil an: %s", profile->profile_name);

    esp_err_t err;

    // 1. Noise Suppressor
    err = dsp_model_set_noise_suppressor(profile->noise_suppressor_enabled);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Noise Suppressor set fehlgeschlagen: %s", esp_err_to_name(err));
    }

    // 2. Virtual Bass
    err = dsp_model_set_virtual_bass(profile->virtual_bass_enabled);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Virtual Bass set fehlgeschlagen: %s", esp_err_to_name(err));
    }

    // 3. Silence Detector
    err = dsp_model_set_silence_detector(profile->silence_detector_enabled);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Silence Detector set fehlgeschlagen: %s", esp_err_to_name(err));
    }

    // 4. PreEQ
    if (profile->preeq.block_enabled != s_current_profile.preeq.block_enabled) {
        dsp_model_set_preeq_enable(profile->preeq.block_enabled);
    }

    // 5. DRC
    if (profile->drc.enabled != s_current_profile.drc.enabled) {
        dsp_model_set_drc_enable(profile->drc.enabled);
    }

    // Profil im Cache speichern
    memcpy(&s_current_profile, profile, sizeof(dsp_profile_t));

    ESP_LOGI(TAG, "DSP-Profil angewendet (kein Flash-Save)");
    return ESP_OK;
}

esp_err_t dsp_model_readback(dsp_profile_t *profile)
{
    if (!profile) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Lese DSP-Zustand aus...");

    uint8_t frame[16];
    uint8_t report[256];
    uint16_t report_len;

    // Noise Suppressor
    mvs_build_query_frame(MVS_EFFECT_NOISE_SUPPRESSOR, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(50));
        err = usb_host_ctrl_get_report(report, &report_len);
        if (err == ESP_OK && report_len >= 16) {
            mvs_decode_noise_suppressor(report + 4, report_len - 4,
                                        &profile->noise_suppressor_enabled,
                                        &profile->noise_suppressor_threshold_raw,
                                        &profile->noise_suppressor_ratio,
                                        &profile->noise_suppressor_attack_ms,
                                        &profile->noise_suppressor_release_ms);
        }
    }

    // Virtual Bass
    mvs_build_query_frame(MVS_EFFECT_VIRTUAL_BASS, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(50));
        err = usb_host_ctrl_get_report(report, &report_len);
        if (err == ESP_OK && report_len >= 10) {
            mvs_decode_virtual_bass(report + 4, report_len - 4,
                                    &profile->virtual_bass_enabled,
                                    &profile->virtual_bass_cutoff_hz,
                                    &profile->virtual_bass_intensity_pct,
                                    &profile->virtual_bass_enhanced);
        }
    }

    // PreEQ (komplex)
    mvs_build_query_frame(MVS_EFFECT_PREEQ, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(50));
        err = usb_host_ctrl_get_report(report, &report_len);
        if (err == ESP_OK && report_len >= 110) {
            // Daten nach dem Header: offset 4
            mvs_decode_preeq(report + 5, report_len - 5, &profile->preeq);
        }
    }

    // DRC (komplex)
    mvs_build_query_frame(MVS_EFFECT_DRC, frame, sizeof(frame));
    mvs_prepare_hid_report(frame, 5, report);
    err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(50));
        err = usb_host_ctrl_get_report(report, &report_len);
        if (err == ESP_OK && report_len >= 58) {
            mvs_decode_drc(report + 5, report_len - 5, &profile->drc);
        }
    }

    memcpy(&s_current_profile, profile, sizeof(dsp_profile_t));
    ESP_LOGI(TAG, "DSP-Readback abgeschlossen");
    return ESP_OK;
}

esp_err_t dsp_model_set_noise_suppressor(bool enable)
{
    ESP_LOGI(TAG, "Noise Suppressor %s", enable ? "EIN" : "AUS");
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(MVS_EFFECT_NOISE_SUPPRESSOR,
                                          MVS_SEL_BLOCK_ENABLE,
                                          enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 7);
}

esp_err_t dsp_model_set_virtual_bass(bool enable)
{
    ESP_LOGI(TAG, "Virtual Bass %s", enable ? "EIN" : "AUS");
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(MVS_EFFECT_VIRTUAL_BASS,
                                          MVS_SEL_BLOCK_ENABLE,
                                          enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 7);
}

esp_err_t dsp_model_set_silence_detector(bool enable)
{
    ESP_LOGI(TAG, "Silence Detector %s", enable ? "EIN" : "AUS");
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(MVS_EFFECT_SILENCE_DETECTOR,
                                          MVS_SEL_BLOCK_ENABLE,
                                          enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 7);
}

esp_err_t dsp_model_set_preeq_enable(bool enable)
{
    ESP_LOGI(TAG, "PreEQ %s", enable ? "EIN" : "AUS");
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(MVS_EFFECT_PREEQ,
                                          MVS_SEL_BLOCK_ENABLE,
                                          enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 7);
}

esp_err_t dsp_model_set_drc_enable(bool enable)
{
    ESP_LOGI(TAG, "DRC %s", enable ? "EIN" : "AUS");
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(MVS_EFFECT_DRC,
                                          MVS_SEL_BLOCK_ENABLE,
                                          enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 7);
}

esp_err_t dsp_model_update_preeq(const mvs_preeq_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "PreEQ-State-Update (vollständiger State)");
    uint8_t frame[128];
    esp_err_t err = mvs_build_preeq_full_frame(state, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 111);
}

esp_err_t dsp_model_update_drc(const mvs_drc_packed_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "DRC-State-Update (vollständiger State)");
    uint8_t frame[64];
    esp_err_t err = mvs_build_drc_full_frame(state, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 59);
}