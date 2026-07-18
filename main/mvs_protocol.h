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

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

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

// Effect IDs
#define MVS_EFFECT_NOISE_SUPPRESSOR  0x88
#define MVS_EFFECT_SILENCE_DETECTOR  0x89
#define MVS_EFFECT_VIRTUAL_BASS      0x97
#define MVS_EFFECT_PREEQ             0x99
#define MVS_EFFECT_DRC               0x9A
#define MVS_EFFECT_PHASE             0x96
#define MVS_EFFECT_TAG               0xFC
#define MVS_EFFECT_SAVE              0xFD

// Command types
#define MVS_CMD_QUERY                0x00
#define MVS_CMD_WRITE                0x03
#define MVS_CMD_READBACK             0x05
#define MVS_CMD_READBACK_FULL        0x0B
#define MVS_CMD_WRITE_FULL_STATE     0x6B
#define MVS_CMD_READBACK_EXTENDED    0x09  /* DRC extended readback */
#define MVS_CMD_READBACK_DOUBLE      0x09

// Selectors for individual parameter writes
#define MVS_SEL_BLOCK_ENABLE         0x00
#define MVS_SEL_PARAM_1              0x01
#define MVS_SEL_PARAM_2              0x02
#define MVS_SEL_PARAM_3              0x03
#define MVS_SEL_PARAM_4              0x04
#define MVS_SEL_FILTER_INDEX         0x02
#define MVS_SEL_FILTER_GAIN          0x07

// ---------------------------------------------------------------------------
// Filtertypen (PreEQ)
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
// Datenstrukturen
// ---------------------------------------------------------------------------

// PreEQ-Filter (10 Bytes im DSP-State)
typedef struct __attribute__((packed)) {
    uint16_t enabled;
    uint16_t type;          // 0-8, siehe MVS_FILTER_*
    uint16_t frequency_hz;
    uint16_t q_raw;         // Q = raw / 1024
    int16_t  gain_raw;      // dB = raw / 256
} mvs_preeq_filter_t;

// Vollständiger PreEQ-State (0x99)
typedef struct __attribute__((packed)) {
    uint16_t block_enabled;
    int16_t  pre_gain_raw;  // Q8.8 dB (raw / 256)
    uint16_t selected_filter;
    mvs_preeq_filter_t filters[10];
} mvs_preeq_state_t;
// Gesamtgröße: 2+2+2+10*10 = 106 Bytes = 0x6A
// Plus 1 Byte Terminator = 0x6B Command

// DRC-Parameter (4 Werte pro Parameter = 4 Pfade)
typedef struct {
    int16_t  threshold_raw[4]; // 0.01 dB, signed
    uint16_t ratio_raw[4];     // 0.01 ratio units (100 = 1.00:1)
    uint16_t attack_ms[4];
    uint16_t release_ms[4];
    uint16_t pregain_raw[4];   // Q4.12 coefficient
} mvs_drc_paths_t;

// DRC-State (0x9A)
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
// Gesamt: 2*7 + 4*4 + 2*4*3 = 14 + 16 + 24 = 54 Bytes = 0x36
//  0:  ena  1:  mode  2:  type  3:  q1  4:  q2  5:  freq1  6:  freq2
//  7-10: threshold  11-14: ratio  15-18: attack  19-22: release  23-26: pregain

// ---------------------------------------------------------------------------
// Öffentliche API
// ---------------------------------------------------------------------------

/**
 * @brief Frame für Effekt-Abfrage (QUERY) erstellen.
 *
 * Erzeugt: A5 5A <effect_id> 00 16
 *
 * @param effect_id MVSilicon-Effect-ID
 * @param buffer Ausgabepuffer (mindestens 5 Byte)
 * @param buf_size Puffergröße
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_build_query_frame(uint8_t effect_id, uint8_t *buffer, size_t buf_size);

/**
 * @brief Frame für einfache Parameter-Writes erstellen.
 *
 * Erzeugt: A5 5A <effect_id> 03 <selector> <value_lo> <value_hi> 16
 *
 * @param effect_id MVSilicon-Effect-ID
 * @param selector Parameterselektor
 * @param value 16-Bit-Wert (Little-Endian)
 * @param buffer Ausgabepuffer (mindestens 8 Byte)
 * @param buf_size Puffergröße
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_build_write_frame(uint8_t effect_id, uint8_t selector,
                                uint16_t value, uint8_t *buffer, size_t buf_size);

/**
 * @brief Frame für vollständiges PreEQ-State-Update erstellen.
 *
 * Erzeugt: A5 5A 99 6B FF <106 Bytes State> 16
 *
 * @param state PreEQ-State-Struktur
 * @param buffer Ausgabepuffer (mindestens 110 Byte)
 * @param buf_size Puffergröße
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_build_preeq_full_frame(const mvs_preeq_state_t *state,
                                     uint8_t *buffer, size_t buf_size);

/**
 * @brief Frame für vollständiges DRC-State-Update erstellen.
 *
 * Erzeugt: A5 5A 9A 37 FF <54 Bytes State> 16 (60 Bytes gesamt)
 *
 * @param state DRC-State-Struktur
 * @param buffer Ausgabepuffer (mindestens 60 Byte)
 * @param buf_size Puffergröße
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_build_drc_full_frame(const mvs_drc_packed_state_t *state,
                                   uint8_t *buffer, size_t buf_size);

/**
 * @brief Tag-Frame erstellen (0xFC).
 *
 * @param tag Nullterminierter Tag-String (max. 32 Byte)
 * @param buffer Ausgabepuffer (mindestens 39 Byte)
 * @param buf_size Puffergröße
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_build_tag_frame(const char *tag, uint8_t *buffer, size_t buf_size);

/**
 * @brief Save-Frame erstellen (0xFD).
 *
 * @param buffer Ausgabepuffer (mindestens 4 Byte)
 * @param buf_size Puffergröße
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_build_save_frame(uint8_t *buffer, size_t buf_size);

/**
 * @brief Readback-Daten für Noise Suppressor (0x88) parsen.
 *
 * @param data Rohe Readback-Daten (nach Header)
 * @param length Datenlänge
 * @param[out] enabled Ob der Block aktiviert ist
 * @param[out] threshold_dB Schwellwert in dB (0.01 dB Einheiten)
 * @param[out] ratio Ratio-Wert
 * @param[out] attack_ms Angriffszeit in ms
 * @param[out] release_ms Release-Zeit in ms
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_decode_noise_suppressor(const uint8_t *data, uint16_t length,
                                      bool *enabled, int16_t *threshold_dB,
                                      uint16_t *ratio,
                                      uint16_t *attack_ms, uint16_t *release_ms);

/**
 * @brief Readback-Daten für Virtual Bass (0x97) parsen.
 *
 * @param data Rohe Readback-Daten
 * @param length Datenlänge
 * @param[out] enabled Block aktiviert
 * @param[out] cutoff_hz Cutoff-Frequenz
 * @param[out] intensity_percent Intensität in %
 * @param[out] bass_enhanced BassEnhanced-Flag
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_decode_virtual_bass(const uint8_t *data, uint16_t length,
                                  bool *enabled, uint16_t *cutoff_hz,
                                  uint16_t *intensity_percent,
                                  bool *bass_enhanced);

/**
 * @brief Readback-Daten für PreEQ (0x99) parsen.
 *
 * @param data Rohe Readback-Daten (nach 0x6B FF)
 * @param length Datenlänge
 * @param[out] state Dekodierter PreEQ-State
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_decode_preeq(const uint8_t *data, uint16_t length,
                           mvs_preeq_state_t *state);

/**
 * @brief Readback-Daten für DRC (0x9A) parsen.
 *
 * @param data Rohe Readback-Daten (nach 0x37 FF)
 * @param length Datenlänge
 * @param[out] state Dekodierter DRC-State
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_decode_drc(const uint8_t *data, uint16_t length,
                         mvs_drc_packed_state_t *state);

/**
 * @brief Frame validieren (Magic, Terminator).
 *
 * @param frame Rohframe (256 Byte)
 * @param length Tatsächliche Länge
 * @return true bei gültigem Frame
 */
bool mvs_validate_frame(const uint8_t *frame, uint16_t length);

/**
 * @brief HID-Report-Puffer vorbereiten (Frame + Padding auf 256 Byte).
 *
 * @param frame Frame-Daten
 * @param frame_len Nutzdatenlänge
 * @param report Ausgabe-HID-Report (256 Byte)
 */
void mvs_prepare_hid_report(const uint8_t *frame, uint16_t frame_len,
                            uint8_t *report);

#ifdef __cplusplus
}
#endif
