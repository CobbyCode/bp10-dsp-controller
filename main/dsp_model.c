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
#include "esp_log_buffer.h"

static const char *TAG = "bp10_dsp";

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
    ESP_LOGI(TAG, "Wende DSP-Profil an...");

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

    memset(profile, 0, sizeof(*profile));

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
            mvs_decode_noise_suppressor(report + 5, report_len - 5,
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
            mvs_decode_virtual_bass(report + 5, report_len - 5,
                                    &profile->virtual_bass_enabled,
                                    &profile->virtual_bass_cutoff_hz,
                                    &profile->virtual_bass_intensity_pct,
                                    &profile->virtual_bass_enhanced);
        }
    }

    // Silence Detector (0x89, validierter Zustand)
    uint16_t silence_amplitude = 0;
    err = dsp_model_read_silence_detector(&profile->silence_detector_enabled,
                                          &silence_amplitude);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Silence-Readback fehlgeschlagen: %s", esp_err_to_name(err));
    }

    // PreEQ (vollständiger, validierter Zustand)
    err = dsp_model_read_preeq(&profile->preeq);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PreEQ-Readback fehlgeschlagen: %s", esp_err_to_name(err));
    }

    // DRC (vollständiger, validierter Zustand)
    err = dsp_model_read_drc(&profile->drc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DRC-Readback fehlgeschlagen: %s", esp_err_to_name(err));
    }

    memcpy(&s_current_profile, profile, sizeof(dsp_profile_t));
    ESP_LOGI(TAG, "DSP-Readback abgeschlossen");
    return ESP_OK;
}

esp_err_t dsp_model_read_preeq(mvs_preeq_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    uint8_t frame[5];
    uint8_t report[256];
    uint16_t report_len = 0;

    mvs_build_query_frame(MVS_EFFECT_PREEQ, frame, sizeof(frame));
    ESP_LOGI(TAG, "PreEQ 0x99 TX: A5 5A 99 00 16");
    mvs_prepare_hid_report(frame, 5, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "PreEQ 0x99 RX: status=%s len=%u", esp_err_to_name(err), report_len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, report, report_len < 112 ? report_len : 112, ESP_LOG_INFO);
    if (report_len < 112 || report[0] != MVS_FRAME_MAGIC_1 ||
        report[1] != MVS_FRAME_MAGIC_2 || report[2] != MVS_EFFECT_PREEQ ||
        report[4] != 0xFF) {
        ESP_LOGW(TAG, "Ungültiger vollständiger PreEQ-Readback (len=%u)", report_len);
        return ESP_ERR_INVALID_RESPONSE;
    }
    err = mvs_decode_preeq(report + 5, report_len - 5, state);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "PreEQ: enabled=%u pregain_raw=%d selected=%u",
                 state->block_enabled, state->pre_gain_raw, state->selected_filter);
        for (int i = 0; i < 10; ++i) {
            const mvs_preeq_filter_t *f = &state->filters[i];
            ESP_LOGI(TAG, "PreEQ F%d: ena=%u type=%u freq=%u gain_raw=%d q_raw=%u",
                     i, f->enabled, f->type, f->frequency_hz, f->gain_raw, f->q_raw);
        }
    }
    return err;
}

esp_err_t dsp_model_read_drc(mvs_drc_packed_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    uint8_t frame[5];
    uint8_t report[256];
    uint16_t report_len = 0;

    esp_err_t err = mvs_build_query_frame(MVS_EFFECT_DRC, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "DRC 0x9A TX: A5 5A 9A 00 16");
    mvs_prepare_hid_report(frame, sizeof(frame), report);
    err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DRC 0x9A TX fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    err = usb_host_ctrl_get_report(report, &report_len);
    ESP_LOGI(TAG, "DRC 0x9A RX: status=%s len=%u", esp_err_to_name(err), report_len);
    if (err != ESP_OK) return err;
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, report, report_len < 64 ? report_len : 64, ESP_LOG_INFO);
    if (report_len < 60 || report[0] != MVS_FRAME_MAGIC_1 ||
        report[1] != MVS_FRAME_MAGIC_2 || report[2] != MVS_EFFECT_DRC ||
        report[4] != 0xFF || report[59] != MVS_FRAME_TERMINATOR) {
        ESP_LOGW(TAG, "DRC 0x9A ungültig (len=%u)", report_len);
        return ESP_ERR_INVALID_RESPONSE;
    }
    err = mvs_decode_drc(report + 5, report_len - 5, state);
    if (err == ESP_OK) {
        ESP_LOGI(TAG,
                 "DRC parsed: enabled=%u mode=%u type=%u q1=%u q2=%u f1=%u f2=%u",
                 state->enabled, state->mode, state->crossover_type,
                 state->crossover_q1_raw, state->crossover_q2_raw,
                 state->crossover_freq1_hz, state->crossover_freq2_hz);
        for (int i = 0; i < 4; ++i) {
            ESP_LOGI(TAG, "DRC path%d: threshold=%d ratio=%u attack=%u release=%u pregain=%u",
                     i, state->thresholds[i], state->ratios[i], state->attacks[i],
                     state->releases[i], state->pregains[i]);
        }
    }
    return err;
}

esp_err_t dsp_model_read_silence_detector(bool *enabled, uint16_t *amplitude)
{
    if (!enabled || !amplitude) return ESP_ERR_INVALID_ARG;
    uint8_t frame[5];
    uint8_t report[256];
    uint16_t report_len = 0;
    esp_err_t err = mvs_build_query_frame(MVS_EFFECT_SILENCE_DETECTOR,
                                           frame, sizeof(frame));
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "Silence 0x89 TX: A5 5A 89 00 16");
    mvs_prepare_hid_report(frame, sizeof(frame), report);
    err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(100));
    err = usb_host_ctrl_get_report(report, &report_len);
    ESP_LOGI(TAG, "Silence 0x89 RX: status=%s len=%u", esp_err_to_name(err), report_len);
    if (err != ESP_OK) return err;
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, report, report_len < 10 ? report_len : 10, ESP_LOG_INFO);
    if (report_len < 10 || report[0] != MVS_FRAME_MAGIC_1 ||
        report[1] != MVS_FRAME_MAGIC_2 ||
        report[2] != MVS_EFFECT_SILENCE_DETECTOR || report[4] != 0xFF) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *enabled = ((uint16_t)report[5] | ((uint16_t)report[6] << 8)) != 0;
    *amplitude = (uint16_t)report[7] | ((uint16_t)report[8] << 8);
    ESP_LOGI(TAG, "Silence 0x89 parsed: enabled=%d amplitude=%u",
             *enabled, *amplitude);
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
        ESP_LOGW(TAG, "Ungültiger Readback für Effekt 0x%02X (len=%u)",
                 effect_id, report_len);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *enabled = ((uint16_t)report[5] | ((uint16_t)report[6] << 8)) != 0;
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
    return send_mvs_command(frame, 8);
}

esp_err_t dsp_model_set_noise_suppressor_state(bool enable,
                                                int16_t threshold_raw,
                                                uint16_t ratio,
                                                uint16_t attack_ms,
                                                uint16_t release_ms)
{
    const uint16_t values[] = {
        enable ? 1U : 0U,
        (uint16_t)threshold_raw,
        ratio,
        attack_ms,
        release_ms,
    };

    for (uint8_t selector = MVS_SEL_BLOCK_ENABLE;
         selector <= MVS_SEL_PARAM_4; selector++) {
        uint8_t frame[8];
        esp_err_t err = mvs_build_write_frame(MVS_EFFECT_NOISE_SUPPRESSOR,
                                              selector, values[selector],
                                              frame, sizeof(frame));
        if (err != ESP_OK) return err;
        err = send_mvs_command(frame, sizeof(frame));
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

esp_err_t dsp_model_set_virtual_bass(bool enable)
{
    ESP_LOGI(TAG, "Virtual Bass %s", enable ? "EIN" : "AUS");
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(MVS_EFFECT_VIRTUAL_BASS,
                                          MVS_SEL_BLOCK_ENABLE,
                                          enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 8);
}

esp_err_t dsp_model_set_virtual_bass_state(bool enable, uint16_t cutoff_hz,
                                            uint16_t intensity_pct,
                                            bool bass_enhanced)
{
    // Disabling is a single command. Sending parameter writes as part of an
    // OFF operation can make this DSP reject the sequence and drop off USB.
    if (!enable) return dsp_model_set_virtual_bass(false);

    // When enabling, turn the block on first; it rejects parameter writes
    // while disabled.
    const uint8_t selectors[] = {
        MVS_SEL_BLOCK_ENABLE,
        MVS_SEL_PARAM_1, MVS_SEL_PARAM_2, MVS_SEL_PARAM_3,
    };
    const uint16_t values[] = {
        1U, cutoff_hz, intensity_pct, bass_enhanced ? 1U : 0U,
    };
    for (size_t i = 0; i < sizeof(selectors) / sizeof(selectors[0]); i++) {
        uint8_t frame[8];
        esp_err_t err = mvs_build_write_frame(MVS_EFFECT_VIRTUAL_BASS,
                                              selectors[i], values[i],
                                              frame, sizeof(frame));
        if (err != ESP_OK) return err;
        err = send_mvs_command(frame, sizeof(frame));
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

esp_err_t dsp_model_set_silence_detector(bool enable)
{
    ESP_LOGI(TAG, "Silence Detector %s", enable ? "EIN" : "AUS");
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(MVS_EFFECT_SILENCE_DETECTOR,
                                          MVS_SEL_BLOCK_ENABLE,
                                          enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 8);
}

esp_err_t dsp_model_set_preeq_enable(bool enable)
{
    ESP_LOGI(TAG, "PreEQ %s", enable ? "EIN" : "AUS");
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(MVS_EFFECT_PREEQ,
                                          MVS_SEL_BLOCK_ENABLE,
                                          enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 8);
}

esp_err_t dsp_model_set_drc_enable(bool enable)
{
    ESP_LOGI(TAG, "DRC %s", enable ? "EIN" : "AUS");
    uint8_t frame[8];
    esp_err_t err = mvs_build_write_frame(MVS_EFFECT_DRC,
                                          MVS_SEL_BLOCK_ENABLE,
                                          enable ? 1 : 0, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 8);
}

esp_err_t dsp_model_update_preeq(const mvs_preeq_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;

    // Defensiver Schutz: Korrumpierte deaktivierte Filter reparieren.
    // Nur Slots mit (disabled && frequency_hz == 0 && q_raw == 0) werden
    // auf neutrale Werte (PK / 20 kHz / 0 dB / Q 0.707) gesetzt.
    // Bereits konfigurierte deaktivierte Filter bleiben unverändert.
    mvs_preeq_state_t normalized = *state;
    int repaired = 0;
    for (int i = 0; i < 10; i++) {
        mvs_preeq_filter_t *f = &normalized.filters[i];
        if (!f->enabled && f->frequency_hz == 0 && f->q_raw == 0) {
            f->type = MVS_FILTER_PK;
            f->frequency_hz = 20000;
            f->q_raw = 724;  // 0.707 * 1024
            f->gain_raw = 0;
            repaired++;
        }
    }
    if (repaired > 0) {
        ESP_LOGI(TAG, "PreEQ: repaired %d corrupted disabled filters to "
                       "neutral values (PK/20kHz/0dB/Q0.707)", repaired);
    }

    ESP_LOGI(TAG, "PreEQ-State-Update (vollständiger State)");
    uint8_t frame[128];
    esp_err_t err = mvs_build_preeq_full_frame(&normalized, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 112);
}

esp_err_t dsp_model_update_drc(const mvs_drc_packed_state_t *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "DRC-State-Update (vollständiger State)");
    uint8_t frame[64];
    esp_err_t err = mvs_build_drc_full_frame(state, frame, sizeof(frame));
    if (err != ESP_OK) return err;
    return send_mvs_command(frame, 60);
}
