// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// mvs_protocol.c — MVSilicon-Protokoll — Encoder/Decoder
//
// Siehe mvs_protocol.h für die vollständige API.
//

#include "mvs_protocol.h"
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "bp10_mvs_proto";

// ---------------------------------------------------------------------------
// Interne Hilfsfunktionen
// ---------------------------------------------------------------------------

static inline void write_u16_le(uint8_t *buf, uint16_t val)
{
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
}

static inline uint16_t read_u16_le(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static inline int16_t read_s16_le(const uint8_t *buf)
{
    return (int16_t)(buf[0] | ((uint16_t)buf[1] << 8));
}

// ---------------------------------------------------------------------------
// Frame-Builder
// ---------------------------------------------------------------------------

static esp_err_t build_header(uint8_t effect_id, uint8_t cmd_len,
                               uint8_t *buffer, size_t buf_size)
{
    if (buf_size < 5) return ESP_ERR_INVALID_SIZE;

    buffer[0] = MVS_FRAME_MAGIC_1;
    buffer[1] = MVS_FRAME_MAGIC_2;
    buffer[2] = effect_id;
    buffer[3] = cmd_len;
    return ESP_OK;
}

static esp_err_t add_terminator(uint8_t *buffer, size_t buf_size, uint8_t pos)
{
    if (pos >= buf_size) return ESP_ERR_INVALID_SIZE;
    buffer[pos] = MVS_FRAME_TERMINATOR;
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Öffentliche API
// ---------------------------------------------------------------------------

esp_err_t mvs_build_query_frame(uint8_t effect_id, uint8_t *buffer, size_t buf_size)
{
    ESP_RETURN_ON_ERROR(build_header(effect_id, 0x00, buffer, buf_size), TAG, "header");
    return add_terminator(buffer, buf_size, 4);
}

esp_err_t mvs_build_write_frame(uint8_t effect_id, uint8_t selector,
                                uint16_t value, uint8_t *buffer, size_t buf_size)
{
    if (buf_size < 8) return ESP_ERR_INVALID_SIZE;

    ESP_RETURN_ON_ERROR(build_header(effect_id, 0x03, buffer, buf_size), TAG, "header");
    buffer[4] = selector;
    write_u16_le(&buffer[5], value);
    return add_terminator(buffer, buf_size, 7);
}

esp_err_t mvs_build_preeq_full_frame(const mvs_preeq_state_t *state,
                                     uint8_t *buffer, size_t buf_size)
{
    // Frame: A5 5A 99 6B FF <106B> 16
    if (buf_size < 112) return ESP_ERR_INVALID_SIZE;

    ESP_RETURN_ON_ERROR(build_header(MVS_EFFECT_PREEQ, 0x6B, buffer, buf_size), TAG, "header");
    buffer[4] = 0xFF;  // Readback-Response-Marker

    // State kopieren (106 Bytes)
    uint8_t *payload = &buffer[5];
    write_u16_le(payload + 0,  state->block_enabled);
    write_u16_le(payload + 2,  (uint16_t)state->pre_gain_raw);
    write_u16_le(payload + 4,  state->selected_filter);

    for (int i = 0; i < 10; i++) {
        int off = 6 + i * 10;
        write_u16_le(payload + off + 0, state->filters[i].enabled);
        write_u16_le(payload + off + 2, state->filters[i].type);
        write_u16_le(payload + off + 4, state->filters[i].frequency_hz);
        write_u16_le(payload + off + 6, state->filters[i].q_raw);
        write_u16_le(payload + off + 8, (uint16_t)state->filters[i].gain_raw);
    }

    return add_terminator(buffer, buf_size, 111);
}

esp_err_t mvs_build_drc_full_frame(const mvs_drc_packed_state_t *state,
                                   uint8_t *buffer, size_t buf_size)
{
    // Frame: A5 5A 9A 37 FF <54B> 16 = 60 Bytes total.
    if (buf_size < 60) return ESP_ERR_INVALID_SIZE;

    ESP_RETURN_ON_ERROR(build_header(MVS_EFFECT_DRC, 0x37, buffer, buf_size), TAG, "header");
    buffer[4] = 0xFF;

    uint8_t *p = &buffer[5];
    write_u16_le(p + 0,  state->enabled);
    write_u16_le(p + 2,  state->mode);
    write_u16_le(p + 4,  state->crossover_type);
    write_u16_le(p + 6,  state->crossover_q1_raw);
    write_u16_le(p + 8,  state->crossover_q2_raw);
    write_u16_le(p + 10, state->crossover_freq1_hz);
    write_u16_le(p + 12, state->crossover_freq2_hz);
    // thresholds[4] at offset 14
    for (int i = 0; i < 4; i++)
        write_u16_le(p + 14 + i*2, (uint16_t)state->thresholds[i]);
    // ratios[4] at offset 22
    for (int i = 0; i < 4; i++)
        write_u16_le(p + 22 + i*2, state->ratios[i]);
    // attacks[4] at offset 30
    for (int i = 0; i < 4; i++)
        write_u16_le(p + 30 + i*2, state->attacks[i]);
    // releases[4] at offset 38
    for (int i = 0; i < 4; i++)
        write_u16_le(p + 38 + i*2, state->releases[i]);
    // pregains[4] at offset 46
    for (int i = 0; i < 4; i++)
        write_u16_le(p + 46 + i*2, state->pregains[i]);

    return add_terminator(buffer, buf_size, 59);
}

esp_err_t mvs_build_tag_frame(const char *tag, uint8_t *buffer, size_t buf_size)
{
    size_t tag_len = strlen(tag);
    if (tag_len > 32) return ESP_ERR_INVALID_SIZE;

    // Frame: A5 5A FC 05 <tag_bytes> 16
    if (buf_size < 5 + tag_len + 1) return ESP_ERR_INVALID_SIZE;

    ESP_RETURN_ON_ERROR(build_header(MVS_EFFECT_TAG, 0x05, buffer, buf_size), TAG, "header");
    memcpy(&buffer[4], tag, tag_len);
    return add_terminator(buffer, buf_size, 4 + tag_len);
}

esp_err_t mvs_build_save_frame(uint8_t *buffer, size_t buf_size)
{
    // Frame: A5 5A FD 00 16
    if (buf_size < 5) return ESP_ERR_INVALID_SIZE;

    ESP_RETURN_ON_ERROR(build_header(MVS_EFFECT_SAVE, 0x00, buffer, buf_size), TAG, "header");
    return add_terminator(buffer, buf_size, 4);
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

esp_err_t mvs_decode_noise_suppressor(const uint8_t *data, uint16_t length,
                                      bool *enabled, int16_t *threshold_dB,
                                      uint16_t *ratio,
                                      uint16_t *attack_ms, uint16_t *release_ms)
{
    // Format: <ena> <thresh_s16> <ratio> <attack> <release>
    // Erwartet Daten ab dem 0xFF-Byte (nach Header)
    if (length < 11) return ESP_ERR_INVALID_SIZE;

    if (enabled)    *enabled     = read_u16_le(data + 0) != 0;
    if (threshold_dB) *threshold_dB = read_s16_le(data + 2);
    if (ratio)      *ratio       = read_u16_le(data + 4);
    if (attack_ms)  *attack_ms   = read_u16_le(data + 6);
    if (release_ms) *release_ms  = read_u16_le(data + 8);

    return ESP_OK;
}

esp_err_t mvs_decode_virtual_bass(const uint8_t *data, uint16_t length,
                                  bool *enabled, uint16_t *cutoff_hz,
                                  uint16_t *intensity_percent,
                                  bool *bass_enhanced)
{
    // Format: <ena> <cutoff> <intensity> <bass_enhanced>
    if (length < 9) return ESP_ERR_INVALID_SIZE;

    if (enabled)         *enabled          = read_u16_le(data + 0) != 0;
    if (cutoff_hz)       *cutoff_hz        = read_u16_le(data + 2);
    if (intensity_percent) *intensity_percent = read_u16_le(data + 4);
    if (bass_enhanced)   *bass_enhanced    = read_u16_le(data + 6) != 0;

    return ESP_OK;
}

esp_err_t mvs_decode_preeq(const uint8_t *data, uint16_t length,
                           mvs_preeq_state_t *state)
{
    // Erwartet 106 Bytes Daten nach 0xFF
    if (!state || length < 106) return ESP_ERR_INVALID_SIZE;

    memset(state, 0, sizeof(*state));

    state->block_enabled  = read_u16_le(data + 0);
    state->pre_gain_raw   = read_s16_le(data + 2);
    state->selected_filter = read_u16_le(data + 4);

    for (int i = 0; i < 10; i++) {
        int off = 6 + i * 10;
        state->filters[i].enabled      = read_u16_le(data + off + 0);
        state->filters[i].type         = read_u16_le(data + off + 2);
        state->filters[i].frequency_hz = read_u16_le(data + off + 4);
        state->filters[i].q_raw        = read_u16_le(data + off + 6);
        state->filters[i].gain_raw     = read_s16_le(data + off + 8);
    }

    return ESP_OK;
}

esp_err_t mvs_decode_drc(const uint8_t *data, uint16_t length,
                         mvs_drc_packed_state_t *state)
{
    // Erwartet 54 Bytes Daten
    if (!state || length < 54) return ESP_ERR_INVALID_SIZE;

    memset(state, 0, sizeof(*state));

    state->enabled           = read_u16_le(data + 0);
    state->mode              = read_u16_le(data + 2);
    state->crossover_type    = read_u16_le(data + 4);
    state->crossover_q1_raw  = read_u16_le(data + 6);
    state->crossover_q2_raw  = read_u16_le(data + 8);
    state->crossover_freq1_hz = read_u16_le(data + 10);
    state->crossover_freq2_hz = read_u16_le(data + 12);

    for (int i = 0; i < 4; i++) {
        state->thresholds[i] = (int16_t)read_u16_le(data + 14 + i*2);
        state->ratios[i]     = read_u16_le(data + 22 + i*2);
        state->attacks[i]    = read_u16_le(data + 30 + i*2);
        state->releases[i]   = read_u16_le(data + 38 + i*2);
        state->pregains[i]   = read_u16_le(data + 46 + i*2);
    }

    return ESP_OK;
}

bool mvs_validate_frame(const uint8_t *frame, uint16_t length)
{
    if (length < 5) return false;
    if (frame[0] != MVS_FRAME_MAGIC_1) return false;
    if (frame[1] != MVS_FRAME_MAGIC_2) return false;

    // Terminator suchen
    for (uint16_t i = 4; i < length; i++) {
        if (frame[i] == MVS_FRAME_TERMINATOR) return true;
    }
    return false;
}

void mvs_prepare_hid_report(const uint8_t *frame, uint16_t frame_len,
                            uint8_t *report)
{
    memset(report, 0, 256);
    if (frame_len > 256) frame_len = 256;
    memcpy(report, frame, frame_len);
}
