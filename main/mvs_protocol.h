// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// mvs_protocol.h — MVSilicon-Protokoll — Encoder/Decoder
//
// Vollständige Implementierung des MVSilicon USB-HID-Protokolls
// gemäß der reverse-engineered Command Reference.
//
// Framing: A5 5A <effect-id> <length/command> [payload...] 16
// Transport: HID SET_REPORT, 256 Byte
//
// Unterstützt:
//   - A800X-Festprofile (feste Effekt-IDs 0x88, 0x89, 0x97, 0x99, 0x9A)
//   - Generic-ACP-Katalogabfrage (0x80/0x81)
//   - Dynamische Effektadressen (0x80 + catalog_index)
//   - A800X-DRC (54 Byte, 4-Pfad)
//   - Classic-DRC (38 Byte, 3-Band)
//   - Classic-PreEQ/Out EQ (106 Byte, 10 Filter, Q8.8/uint16)
//   - USB Out Gain (6 Byte full read, Q4.12 gain)
//   - Generischer Array-Write-Builder
//   - Multi-Path (Music / REC)

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "mvs_device_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Protokollkonstanten
// ---------------------------------------------------------------------------

#define MVS_FRAME_MAGIC_1         0xA5
#define MVS_FRAME_MAGIC_2         0x5A
#define MVS_FRAME_TERMINATOR      0x16
#define MVS_FRAME_MAX_PAYLOAD     253   // 256 - 4 (magic+id+len+term)
#define MVS_CATALOG_MAX_EFFECTS   64

// A800X-Effekt-IDs (fest)
#define MVS_EFFECT_NOISE_SUPPRESSOR  0x88
#define MVS_EFFECT_SILENCE_DETECTOR  0x89
#define MVS_EFFECT_VIRTUAL_BASS      0x97
#define MVS_EFFECT_PREEQ             0x99
#define MVS_EFFECT_DRC               0x9A
#define MVS_EFFECT_PHASE             0x96
#define MVS_EFFECT_TAG               0xFC
#define MVS_EFFECT_SAVE              0xFD

// Katalog-Befehle
#define MVS_CATALOG_REQUEST          0x80
#define MVS_CATALOG_NAMEREAD         0x81

// Command types
#define MVS_CMD_QUERY                0x00
#define MVS_CMD_WRITE                0x03
#define MVS_CMD_READBACK             0x05
#define MVS_CMD_READBACK_FULL        0x0B
#define MVS_CMD_WRITE_FULL_STATE     0x6B
#define MVS_CMD_READBACK_EXTENDED    0x09
#define MVS_CMD_READBACK_DOUBLE      0x09

// Selectors for individual parameter writes
#define MVS_SEL_BLOCK_ENABLE         0x00
#define MVS_SEL_PARAM_1              0x01
#define MVS_SEL_PARAM_2              0x02
#define MVS_SEL_PARAM_3              0x03
#define MVS_SEL_PARAM_4              0x04
#define MVS_SEL_FILTER_INDEX         0x02
#define MVS_SEL_FILTER_GAIN          0x07

// USB Out Gain selectors
#define MVS_SEL_USB_GAIN_OUTPUT      0x02

// ---------------------------------------------------------------------------
// Filtertypen (PreEQ / Out EQ)
// ---------------------------------------------------------------------------
#define MVS_FILTER_PK    0  // Peaking
#define MVS_FILTER_LS    1  // Low Shelf
#define MVS_FILTER_HS    2  // High Shelf
#define MVS_FILTER_LP    3  // Low Pass
#define MVS_FILTER_HP    4  // High Pass
#define MVS_FILTER_BP    5  // Band Pass
#define MVS_FILTER_NH    6  // Notch
#define MVS_FILTER_LO    7  // (ACP-Label)
#define MVS_FILTER_HO    8  // (ACP-Label)

// ---------------------------------------------------------------------------
// Datenstrukturen — A800X
// ---------------------------------------------------------------------------

// PreEQ-Filter (10 Bytes im DSP-State)
typedef struct __attribute__((packed)) {
    uint16_t enabled;
    uint16_t type;          // 0-8, siehe MVS_FILTER_*
    uint16_t frequency_hz;
    uint16_t q_raw;         // Q = raw / 1024
    int16_t  gain_raw;      // dB = raw / 256
} mvs_preeq_filter_t;

// Vollständiger PreEQ/OutEQ-State — A800X und Classic gemeinsam
typedef struct __attribute__((packed)) {
    uint16_t block_enabled;
    int16_t  pre_gain_raw;  // Q8.8 dB (raw / 256)
    uint16_t selected_filter;
    mvs_preeq_filter_t filters[10];
} mvs_preeq_state_t;

// DRC-Parameter (4 Werte pro Parameter = 4 Pfade) — A800X
typedef struct {
    int16_t  threshold_raw[4]; // 0.01 dB, signed
    uint16_t ratio_raw[4];     // 0.01 ratio units (100 = 1.00:1)
    uint16_t attack_ms[4];
    uint16_t release_ms[4];
    uint16_t pregain_raw[4];   // Q4.12 coefficient
} mvs_drc_paths_t;

// DRC-State (0x9A) — A800X 4-Pfad, 54 Byte
typedef struct __attribute__((packed)) {
    uint16_t enabled;
    uint16_t mode;             // 0 = Full Band, 1-4 = Multiband
    uint16_t crossover_type;   // 1-4
    uint16_t crossover_q1_raw; // Q = raw / 1024
    uint16_t crossover_q2_raw;
    uint16_t crossover_freq1_hz;
    uint16_t crossover_freq2_hz;
    int16_t  thresholds[4];    // 0.01 dB, signed
    uint16_t ratios[4];        // 0.01 ratio units
    uint16_t attacks[4];        // ms
    uint16_t releases[4];       // ms
    uint16_t pregains[4];       // Q4.12 coefficient
} mvs_drc_packed_state_t;

// ---------------------------------------------------------------------------
// Gemeinsamer DRC-State (38 Byte Payload / 39 Byte inkl. Full-Read-Marker)
// ---------------------------------------------------------------------------

typedef struct __attribute__((packed)) {
    uint16_t enabled;
    uint16_t crossover_hz;
    uint16_t mode;
    uint16_t q_lp_raw;       // Q6.10
    uint16_t q_hp_raw;       // Q6.10
    int16_t  thresholds[3];  // Lower, Upper, Full; Centi-dB
    uint16_t ratios[3];      // Lower, Upper, Full; direct ratio
    uint16_t attacks[3];
    uint16_t releases[3];
    uint16_t pregain_lower;  // Q4.12
    uint16_t pregain_upper;  // Q4.12
} mvs_drc_state_t;

typedef enum {
    MVS_DRC_BAND_LOWER = 0,
    MVS_DRC_BAND_UPPER = 1,
    MVS_DRC_BAND_FULL  = 2,
    MVS_DRC_BAND_COUNT = 3,
} mvs_drc_band_t;

typedef struct {
    double pregain_db;
    double threshold_db;
    double ratio;
    uint16_t attack_ms;
    uint16_t release_ms;
} dsp_drc_band_view_t;

// ---------------------------------------------------------------------------
// USB Out Gain State
// ---------------------------------------------------------------------------

typedef struct __attribute__((packed)) {
    uint16_t enable;       // enable/bypass field
    uint16_t reserved;     // reserved field, never use as gain
    uint16_t gain_raw;     // Q4.12 linear gain coefficient
} mvs_usb_out_gain_state_t;

// ---------------------------------------------------------------------------
// Normalisierte, modefähige gemeinsame DRC-Ansicht
// ---------------------------------------------------------------------------

typedef struct {
    bool valid;
    bool enabled;
    uint16_t mode;
    bool lower_upper_visible;
    bool full_band_supported;
    bool crossover_visible;
    bool q_visible;
    uint16_t crossover_hz;
    double q_lp;
    double q_hp;
    dsp_drc_band_view_t bands[MVS_DRC_BAND_COUNT];
} dsp_drc_view_t;

// ---------------------------------------------------------------------------
// Öffentliche API — Frame-Builder
// ---------------------------------------------------------------------------

esp_err_t mvs_build_query_frame(uint8_t effect_id, uint8_t *buffer, size_t buf_size);
esp_err_t mvs_build_write_frame(uint8_t effect_id, uint8_t selector,
                                uint16_t value, uint8_t *buffer, size_t buf_size);
esp_err_t mvs_build_preeq_full_frame_dyn(uint8_t effect_id,
                                          const mvs_preeq_state_t *state,
                                          uint8_t *buffer, size_t buf_size);

static inline esp_err_t mvs_build_preeq_full_frame(const mvs_preeq_state_t *state,
                                                    uint8_t *buffer, size_t buf_size)
{
    return mvs_build_preeq_full_frame_dyn(MVS_EFFECT_PREEQ, state, buffer, buf_size);
}

esp_err_t mvs_build_drc_a800x_full_frame(uint8_t effect_id,
                                          const mvs_drc_packed_state_t *state,
                                          uint8_t *buffer, size_t buf_size);

static inline esp_err_t mvs_build_drc_full_frame(const mvs_drc_packed_state_t *state,
                                                   uint8_t *buffer, size_t buf_size)
{
    return mvs_build_drc_a800x_full_frame(MVS_EFFECT_DRC, state, buffer, buf_size);
}

esp_err_t mvs_build_write_u16_array_frame(
    uint8_t effect_id, uint8_t selector,
    const uint16_t *values, size_t value_count,
    uint8_t *buffer, size_t buf_size, size_t *frame_len);

esp_err_t mvs_build_tag_frame(const char *tag, uint8_t *buffer, size_t buf_size);
esp_err_t mvs_build_save_frame(uint8_t *buffer, size_t buf_size);

// ---------------------------------------------------------------------------
// Katalog und Discovery
// ---------------------------------------------------------------------------

esp_err_t mvs_build_catalog_request_frame(uint8_t selector,
                                           uint8_t *buffer, size_t buf_size);
esp_err_t mvs_parse_catalog_list(const uint8_t *data, uint16_t length,
                                  uint8_t *effect_count,
                                  uint16_t *effect_types, uint8_t max_effects);
void mvs_normalize_catalog_name(const uint8_t *name, uint16_t name_len,
                                 char *out, size_t out_max);

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

esp_err_t mvs_decode_noise_suppressor(const uint8_t *data, uint16_t length,
                                      bool *enabled, int16_t *threshold_dB,
                                      uint16_t *ratio,
                                      uint16_t *attack_ms, uint16_t *release_ms);
esp_err_t mvs_decode_virtual_bass(const uint8_t *data, uint16_t length,
                                  bool *enabled, uint16_t *cutoff_hz,
                                  uint16_t *intensity_percent,
                                  bool *bass_enhanced);
esp_err_t mvs_decode_virtual_bass_classic(const uint8_t *data, uint16_t length,
                                          bool *enabled, uint16_t *cutoff_hz,
                                          uint16_t *intensity_percent);
esp_err_t mvs_decode_phase(const uint8_t *data, uint16_t length,
                           bool *phase_invert);
esp_err_t mvs_decode_delay(const uint8_t *data, uint16_t length,
                           bool *enabled, uint16_t *delay_ms,
                           bool *hq_enabled);
esp_err_t mvs_decode_preeq(const uint8_t *data, uint16_t length,
                           mvs_preeq_state_t *state);
esp_err_t mvs_decode_drc_a800x(const uint8_t *data, uint16_t length,
                                mvs_drc_packed_state_t *state);

static inline esp_err_t mvs_decode_drc(const uint8_t *data, uint16_t length,
                                        mvs_drc_packed_state_t *state)
{
    return mvs_decode_drc_a800x(data, length, state);
}

esp_err_t mvs_decode_drc_state(const uint8_t *data, uint16_t length,
                               mvs_drc_state_t *state);
esp_err_t mvs_drc_state_to_view(const mvs_drc_state_t *state,
                                dsp_drc_view_t *view);
esp_err_t mvs_drc_a800x_to_view(const mvs_drc_packed_state_t *state,
                                 dsp_drc_view_t *view);

// ---------------------------------------------------------------------------
// USB Out Gain
// ---------------------------------------------------------------------------

/**
 * @brief USB Out Gain Readback decodieren.
 *
 * Format: <enable/bypass:u16> <reserved:u16> <gain:u16>
 * gain_raw = Q4.12 linear gain coefficient
 * gain_db = 20 * log10(gain_raw / 4096.0)
 * gain_raw = round(4096 * 10^(gain_db / 20))
 *
 * @param data Rohe Readback-Daten
 * @param length Datenlänge (exakt 6)
 * @param[out] gain_raw Lineares Gain (Q4.12)
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_decode_usb_out_gain(const uint8_t *data, uint16_t length,
                                   uint16_t *gain_raw);

// ---------------------------------------------------------------------------
// Hilfsfunktionen
// ---------------------------------------------------------------------------

bool mvs_validate_frame(const uint8_t *frame, uint16_t length);
void mvs_prepare_hid_report(const uint8_t *frame, uint16_t frame_len,
                            uint8_t *report);

// ---------------------------------------------------------------------------
// PreEQ-Schema-Adapter (auch für Out EQ)
// ---------------------------------------------------------------------------

void mvs_prepare_preeq_for_schema(mvs_preeq_schema_t schema,
                                  mvs_preeq_state_t *state);

#ifdef __cplusplus
}
#endif
