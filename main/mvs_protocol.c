// SPDX-FileCopyrightText: 2026 CobbyCode
// SPDX-License-Identifier: GPL-3.0-or-later
//
// mvs_protocol.c — MVSilicon-Protokoll — Encoder/Decoder
//
// Siehe mvs_protocol.h für die vollständige API.
//
// Unterstützt:
//   - A800X-Festprofile
//   - Generic-ACP-Katalogabfrage (0x80/0x81)
//   - Dynamische Effektadressen
//   - Classic-DRC (38 Byte, 3-Band)
//   - Array-Write-Builder
//   - PreEQ-Schema-Adapter
//

#include "mvs_protocol.h"
#include <string.h>
#include <ctype.h>
#include <math.h>
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
// Frame-Builder (Basisfunktionen)
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
// Öffentliche API — Frame-Builder
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

esp_err_t mvs_build_preeq_full_frame_dyn(uint8_t effect_id,
                                          const mvs_preeq_state_t *state,
                                          uint8_t *buffer, size_t buf_size)
{
    // Frame: A5 5A <id> 6B FF <106B> 16 = 112 Bytes total
    if (buf_size < 112) return ESP_ERR_INVALID_SIZE;

    ESP_RETURN_ON_ERROR(build_header(effect_id, 0x6B, buffer, buf_size), TAG, "header");
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

esp_err_t mvs_build_drc_a800x_full_frame(uint8_t effect_id,
                                          const mvs_drc_packed_state_t *state,
                                          uint8_t *buffer, size_t buf_size)
{
    // Frame: A5 5A <id> 37 FF <54B> 16 = 60 Bytes total
    if (buf_size < 60) return ESP_ERR_INVALID_SIZE;

    ESP_RETURN_ON_ERROR(build_header(effect_id, 0x37, buffer, buf_size), TAG, "header");
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

esp_err_t mvs_build_write_u16_array_frame(
    uint8_t effect_id,
    uint8_t selector,
    const uint16_t *values,
    size_t value_count,
    uint8_t *buffer,
    size_t buf_size,
    size_t *frame_len)
{
    // Frame: A5 5A <id> <1 + 2×count> <selector> <values...> 16
    if (value_count == 0 || value_count > 64) return ESP_ERR_INVALID_SIZE;
    size_t cmd_len = 1 + 2 * value_count;  // selector + values
    size_t total = 5 + cmd_len; // header(4) + cmd_len + terminator(1)

    if (buf_size < total || cmd_len > 253) return ESP_ERR_INVALID_SIZE;

    ESP_RETURN_ON_ERROR(build_header(effect_id, (uint8_t)cmd_len, buffer, buf_size), TAG, "header");
    buffer[4] = selector;
    for (size_t i = 0; i < value_count; i++) {
        write_u16_le(&buffer[5 + i * 2], values[i]);
    }

    esp_err_t err = add_terminator(buffer, buf_size, (uint8_t)(total - 1));
    if (err == ESP_OK && frame_len) *frame_len = total;
    return err;
}

esp_err_t mvs_build_tag_frame(const char *tag, uint8_t *buffer, size_t buf_size)
{
    size_t tag_len = strlen(tag);
    if (tag_len > 32) return ESP_ERR_INVALID_SIZE;

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
// Katalog und Discovery
// ---------------------------------------------------------------------------

esp_err_t mvs_build_catalog_request_frame(uint8_t selector,
                                           uint8_t *buffer, size_t buf_size)
{
    // A5 5A 80 01 <selector> 16
    if (buf_size < 6) return ESP_ERR_INVALID_SIZE;

    ESP_RETURN_ON_ERROR(build_header(MVS_CATALOG_REQUEST, 0x01, buffer, buf_size), TAG, "header");
    buffer[4] = selector;
    return add_terminator(buffer, buf_size, 5);
}

esp_err_t mvs_parse_catalog_list(const uint8_t *data, uint16_t length,
                                  uint8_t *effect_count,
                                  uint16_t *effect_types, uint8_t max_effects)
{
    if (!data || !effect_count || !effect_types) return ESP_ERR_INVALID_ARG;
    if (length < 7 || data[0] != MVS_FRAME_MAGIC_1 ||
        data[1] != MVS_FRAME_MAGIC_2 || data[2] != MVS_CATALOG_REQUEST)
        return ESP_ERR_INVALID_RESPONSE;
    uint8_t payload_length = data[3];
    if (payload_length < 2 || (size_t)payload_length + 5U > length ||
        data[4] != 0 || data[4U + payload_length] != MVS_FRAME_TERMINATOR)
        return ESP_ERR_INVALID_RESPONSE;
    uint8_t count = data[5];
    if (count > MVS_CATALOG_MAX_EFFECTS || count > max_effects ||
        2U + (size_t)count * 2U > payload_length)
        return ESP_ERR_INVALID_SIZE;
    *effect_count = count;
    for (uint8_t i = 0; i < count; i++)
        effect_types[i] = read_u16_le(data + 6U + i * 2U);

    return ESP_OK;
}

void mvs_normalize_catalog_name(const uint8_t *name, uint16_t name_len,
                                 char *out, size_t out_max)
{
    if (!name || !out || out_max == 0) return;

    memset(out, 0, out_max);

    // Präfix wie "2:Music Noise Suppressor " oder "4:Music Pre EQ " entfernen
    // Suchen nach ':' und alles davor überspringen
    uint16_t start = 0;
    for (uint16_t i = 0; i < name_len; i++) {
        if (name[i] == ':') {
            start = i + 1;
            break;
        }
    }

    // Kopieren bis Nullbyte oder Ende
    size_t out_idx = 0;
    for (uint16_t i = start; i < name_len && out_idx < out_max - 1; i++) {
        if (name[i] == 0) break;  // Nullbyte = Ende
        out[out_idx++] = (char)name[i];
    }
    out[out_idx] = '\0';

    // Leerzeichen am Ende entfernen
    while (out_idx > 0 && out[out_idx - 1] == ' ') {
        out[--out_idx] = '\0';
    }
}

// ---------------------------------------------------------------------------
// Decoder — Noise Suppressor
// ---------------------------------------------------------------------------

esp_err_t mvs_decode_noise_suppressor(const uint8_t *data, uint16_t length,
                                      bool *enabled, int16_t *threshold_dB,
                                      uint16_t *ratio,
                                      uint16_t *attack_ms, uint16_t *release_ms)
{
    // Format: <ena(2)> <thresh_s16(2)> <ratio(2)> <attack(2)> <release(2)>
    // Exakt 10 Byte erwartet
    if (length < 10) return ESP_ERR_INVALID_SIZE;

    if (enabled)    *enabled     = read_u16_le(data + 0) != 0;
    if (threshold_dB) *threshold_dB = read_s16_le(data + 2);
    if (ratio)      *ratio       = read_u16_le(data + 4);
    if (attack_ms)  *attack_ms   = read_u16_le(data + 6);
    if (release_ms) *release_ms  = read_u16_le(data + 8);

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Decoder — Virtual Bass
// ---------------------------------------------------------------------------

esp_err_t mvs_decode_virtual_bass(const uint8_t *data, uint16_t length,
                                  bool *enabled, uint16_t *cutoff_hz,
                                  uint16_t *intensity_percent,
                                  bool *bass_enhanced)
{
    // Format: <ena(2)> <cutoff(2)> <intensity(2)> <enhanced(2)>
    // Exakt 8 Byte erwartet
    if (length < 8) return ESP_ERR_INVALID_SIZE;

    if (enabled)         *enabled          = read_u16_le(data + 0) != 0;
    if (cutoff_hz)       *cutoff_hz        = read_u16_le(data + 2);
    if (intensity_percent) *intensity_percent = read_u16_le(data + 4);
    if (bass_enhanced)   *bass_enhanced    = read_u16_le(data + 6) != 0;

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Decoder — PreEQ
// ---------------------------------------------------------------------------

esp_err_t mvs_decode_preeq(const uint8_t *data, uint16_t length,
                           mvs_preeq_state_t *state)
{
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

// ---------------------------------------------------------------------------
// Decoder — A800X DRC (54 Byte, 4-Pfad)
// ---------------------------------------------------------------------------

esp_err_t mvs_decode_drc_a800x(const uint8_t *data, uint16_t length,
                                mvs_drc_packed_state_t *state)
{
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

// ---------------------------------------------------------------------------
// Decoder — Classic DRC (38 Byte, 3-Band)
// ---------------------------------------------------------------------------

esp_err_t mvs_decode_drc_classic(const uint8_t *data, uint16_t length,
                                  mvs_drc_classic_state_t *state)
{
    if (!state || length < 38) return ESP_ERR_INVALID_SIZE;

    memset(state, 0, sizeof(*state));

    state->enabled = read_u16_le(data + 0);
    state->fc      = read_u16_le(data + 2);
    state->mode    = read_u16_le(data + 4);
    state->q[0]    = read_u16_le(data + 6);
    state->q[1]    = read_u16_le(data + 8);

    for (int i = 0; i < 3; i++) {
        state->thresholds[i] = (int16_t)read_u16_le(data + 10 + i*2);
        state->ratios[i]     = read_u16_le(data + 16 + i*2);
        state->attacks[i]    = read_u16_le(data + 22 + i*2);
        state->releases[i]   = read_u16_le(data + 28 + i*2);
    }

    state->pregain1 = read_u16_le(data + 34);
    state->pregain2 = read_u16_le(data + 36);

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// DRC-View-Adapter
// ---------------------------------------------------------------------------

esp_err_t mvs_drc_classic_to_view(const mvs_drc_classic_state_t *state,
                                   dsp_drc_view_t *view)
{
    if (!state || !view) return ESP_ERR_INVALID_ARG;
    memset(view, 0, sizeof(*view));

    if (state->mode != 2) {
        // Full-Band mode = 2, index = 2
        view->valid = false;
        view->full_band_supported = false;
        return ESP_ERR_INVALID_ARG;
    }

    const int fb = 2;  // Full-Band index = 2
    view->valid = true;
    view->enabled = state->enabled != 0;
    view->full_band_supported = true;
    view->pregain_db = (state->pregain1 / 4096.0) > 0.0
                       ? 20.0 * log10(state->pregain1 / 4096.0)
                       : -72.0;
    view->threshold_db = state->thresholds[fb] / 100.0;
    view->ratio = state->ratios[fb];  // Direktwert
    view->attack_ms = state->attacks[fb];
    view->release_ms = state->releases[fb];

    return ESP_OK;
}

esp_err_t mvs_drc_a800x_to_view(const mvs_drc_packed_state_t *state,
                                 dsp_drc_view_t *view)
{
    if (!state || !view) return ESP_ERR_INVALID_ARG;
    memset(view, 0, sizeof(*view));

    const int fb = 3;  // A800X Full-Band index = 3
    view->valid = true;
    view->enabled = state->enabled != 0;
    view->full_band_supported = (state->mode == 0);
    view->pregain_db = (state->pregains[fb] / 4096.0) > 0.0
                       ? 20.0 * log10(state->pregains[fb] / 4096.0)
                       : -72.0;
    view->threshold_db = state->thresholds[fb] / 100.0;
    view->ratio = state->ratios[fb] / 100.0;
    view->attack_ms = state->attacks[fb];
    view->release_ms = state->releases[fb];

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// PreEQ-Schema-Adapter
// ---------------------------------------------------------------------------

void mvs_prepare_preeq_for_schema(mvs_preeq_schema_t schema,
                                  mvs_preeq_state_t *state)
{
    if (!state) return;

    if (schema == MVS_PEQ_SCHEMA_CLASSIC_10BAND) {
        // selected_filter = Anzahl aktiver Filter
        uint16_t active = 0;
        for (int i = 0; i < 10; i++) {
            if (state->filters[i].enabled) active++;
        }
        state->selected_filter = active;
    }
    // A800X retains its existing selected_filter semantics.
}

// ---------------------------------------------------------------------------
// Validierung
// ---------------------------------------------------------------------------

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
