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
//   - Classic-PreEQ (106 Byte, 10 Filter, Q8.8/uint16)
//   - Generischer Array-Write-Builder
//

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

// Vollständiger PreEQ-State (0x99) — A800X und Classic gemeinsam
typedef struct __attribute__((packed)) {
    uint16_t block_enabled;
    int16_t  pre_gain_raw;  // Q8.8 dB (raw / 256)
    uint16_t selected_filter;
    mvs_preeq_filter_t filters[10];
} mvs_preeq_state_t;
// Gesamtgröße: 2+2+2+10*10 = 106 Bytes = 0x6A
// Plus 1 Byte Terminator = 0x6B Command

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
// Gesamt: 2*7 + 4*4 + 2*4*3 = 14 + 16 + 24 = 54 Bytes = 0x36
//  0:  ena  1:  mode  2:  type  3:  q1  4:  q2  5:  freq1  6:  freq2
//  7-10: threshold  11-14: ratio  15-18: attack  19-22: release  23-26: pregain

// ---------------------------------------------------------------------------
// Datenstrukturen — Classic DRC (38 Byte, 3-Band)
// ---------------------------------------------------------------------------

typedef struct __attribute__((packed)) {
    uint16_t enabled;
    uint16_t fc;             // Crossover-Frequenz
    uint16_t mode;           // 2 = Full-Band
    uint16_t q[2];           // Q1, Q2
    int16_t  thresholds[3];  // 3 Bänder, Centi-dB
    uint16_t ratios[3];      // Direktwert (20 = 20:1)
    uint16_t attacks[3];     // ms
    uint16_t releases[3];    // ms
    uint16_t pregain1;       // Q4.12
    uint16_t pregain2;       // Q4.12
} mvs_drc_classic_state_t;

// ---------------------------------------------------------------------------
// Normalisierte DRC-Ansicht (Full-Band, schemaunabhängig)
// ---------------------------------------------------------------------------

typedef struct {
    bool valid;
    bool enabled;
    bool full_band_supported;

    double pregain_db;
    double threshold_db;
    double ratio;
    uint16_t attack_ms;
    uint16_t release_ms;
} dsp_drc_view_t;

// ---------------------------------------------------------------------------
// Öffentliche API — Frame-Builder
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
 * Für A800X:  A5 5A 99 6B FF <106 Bytes State> 16
 * Für Generic: A5 5A <dyn_id> 6B FF <106 Bytes State> 16
 *
 * @param effect_id MVSilicon-Effect-ID (dynamisch)
 * @param state PreEQ-State-Struktur
 * @param buffer Ausgabepuffer (mindestens 112 Byte)
 * @param buf_size Puffergröße
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_build_preeq_full_frame_dyn(uint8_t effect_id,
                                          const mvs_preeq_state_t *state,
                                          uint8_t *buffer, size_t buf_size);

/**
 * @brief A800X-spezifischer PreEQ-Full-Frame-Builder (Effect-ID 0x99).
 */
static inline esp_err_t mvs_build_preeq_full_frame(const mvs_preeq_state_t *state,
                                                    uint8_t *buffer, size_t buf_size)
{
    return mvs_build_preeq_full_frame_dyn(MVS_EFFECT_PREEQ, state, buffer, buf_size);
}

/**
 * @brief Frame für A800X-DRC-State-Update erstellen (54 Byte, 4-Pfad).
 *
 * A5 5A <effect_id> 37 FF <54 Bytes State> 16 (60 Bytes gesamt)
 *
 * @param effect_id MVSilicon-Effect-ID (dynamisch)
 * @param state A800X-DRC-State-Struktur
 * @param buffer Ausgabepuffer (mindestens 60 Byte)
 * @param buf_size Puffergröße
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_build_drc_a800x_full_frame(uint8_t effect_id,
                                          const mvs_drc_packed_state_t *state,
                                          uint8_t *buffer, size_t buf_size);

/**
 * @brief A800X-spezifischer DRC-Full-Frame-Builder (Effect-ID 0x9A).
 */
static inline esp_err_t mvs_build_drc_full_frame(const mvs_drc_packed_state_t *state,
                                                   uint8_t *buffer, size_t buf_size)
{
    return mvs_build_drc_a800x_full_frame(MVS_EFFECT_DRC, state, buffer, buf_size);
}

/**
 * @brief Generischer Array-Write-Builder.
 *
 * Erzeugt: A5 5A <effect_id> <1 + 2×count> <selector> <values...> 16
 *
 * @param effect_id MVSilicon-Effect-ID
 * @param selector Parameterselektor
 * @param values Array von uint16-Werten
 * @param value_count Anzahl der Werte
 * @param buffer Ausgabepuffer
 * @param buf_size Puffergröße
 * @param[out] frame_len Tatsächliche Framelänge
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_build_write_u16_array_frame(
    uint8_t effect_id,
    uint8_t selector,
    const uint16_t *values,
    size_t value_count,
    uint8_t *buffer,
    size_t buf_size,
    size_t *frame_len);

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

// ---------------------------------------------------------------------------
// Katalog und Discovery
// ---------------------------------------------------------------------------

/**
 * @brief Katalog-Request-Frame erstellen.
 *
 * A5 5A 80 01 <selector> 16
 * selector=0: Effektanzahl, selector=1..N: Effektname für Index N
 *
 * @param selector 0 = Katalogübersicht, 1..N = Name für Index N
 * @param buffer Ausgabepuffer
 * @param buf_size Puffergröße
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_build_catalog_request_frame(uint8_t selector,
                                           uint8_t *buffer, size_t buf_size);

/**
 * @brief Katalog-Antwort parsen (selector=0).
 *
 * @param data Rohdaten (nach Header)
 * @param length Datenlänge
 * @param[out] effect_count Anzahl der Effekte
 * @param[out] effect_types Array mit Effekt-Typen (max 64)
 * @param max_effects Array-Größe
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_parse_catalog_list(const uint8_t *data, uint16_t length,
                                  uint8_t *effect_count,
                                  uint16_t *effect_types, uint8_t max_effects);

/**
 * @brief Effektnamen aus Katalog-Antwort normalisieren.
 *
 * Entfernt Präfixe (z.B. "2:Music Noise Suppressor " → "Music Noise Suppressor")
 * und trimmt Leerzeichen.
 *
 * @param name Rohdaten des Namens (nach Index-Byte)
 * @param name_len Länge der Rohdaten
 * @param[out] out Normalisierter Name (max 64 Byte)
 * @param out_max Puffergröße
 */
void mvs_normalize_catalog_name(const uint8_t *name, uint16_t name_len,
                                 char *out, size_t out_max);

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

/**
 * @brief Readback-Daten für Noise Suppressor parsen.
 *
 * Erwartet exakt 10 Byte (ab FF-Marker)
 *
 * @param data Rohe Readback-Daten (nach Header)
 * @param length Datenlänge
 * @param[out] enabled Ob der Block aktiviert ist
 * @param[out] threshold_dB Schwellwert in dB (0.01 dB Einheiten)
 * @param[out] ratio Ratio-Wert
 * @param[out] attack_ms Angriffszeit in ms
 * @param[out] release_ms Release-Zeit in ms
 * @return ESP_OK bei Erfolg, ESP_ERR_INVALID_SIZE bei < 10 Byte
 */
esp_err_t mvs_decode_noise_suppressor(const uint8_t *data, uint16_t length,
                                      bool *enabled, int16_t *threshold_dB,
                                      uint16_t *ratio,
                                      uint16_t *attack_ms, uint16_t *release_ms);

/**
 * @brief Readback-Daten für Virtual Bass parsen.
 *
 * Erwartet exakt 8 Byte (ab FF-Marker)
 *
 * @param data Rohe Readback-Daten
 * @param length Datenlänge
 * @param[out] enabled Block aktiviert
 * @param[out] cutoff_hz Cutoff-Frequenz
 * @param[out] intensity_percent Intensität in %
 * @param[out] bass_enhanced BassEnhanced-Flag
 * @return ESP_OK bei Erfolg, ESP_ERR_INVALID_SIZE bei < 8 Byte
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
 * @brief Readback-Daten für A800X-DRC (0x9A) parsen (54 Byte).
 *
 * @param data Rohe Readback-Daten (nach 0x37 FF)
 * @param length Datenlänge
 * @param[out] state Dekodierter DRC-State
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_decode_drc_a800x(const uint8_t *data, uint16_t length,
                                mvs_drc_packed_state_t *state);

/**
 * @brief A800X-DRC-Decoder (Alias, Effect-ID 0x9A).
 */
static inline esp_err_t mvs_decode_drc(const uint8_t *data, uint16_t length,
                                        mvs_drc_packed_state_t *state)
{
    return mvs_decode_drc_a800x(data, length, state);
}

/**
 * @brief Classic-DRC-Readback parsen (38 Byte).
 *
 * @param data Rohe Readback-Daten (nach Header)
 * @param length Datenlänge
 * @param[out] state Dekodierter Classic-DRC-State
 * @return ESP_OK bei Erfolg, ESP_ERR_INVALID_SIZE bei < 38 Byte
 */
esp_err_t mvs_decode_drc_classic(const uint8_t *data, uint16_t length,
                                  mvs_drc_classic_state_t *state);

/**
 * @brief Classic-DRC auf Full-Band-Ansicht normalisieren.
 *
 * @param state Classic-DRC-State
 * @param[out] view Normalisierte Full-Band-Ansicht
 * @return ESP_OK bei Erfolg, ESP_ERR_INVALID_ARG bei mode != 2
 */
esp_err_t mvs_drc_classic_to_view(const mvs_drc_classic_state_t *state,
                                   dsp_drc_view_t *view);

/**
 * @brief A800X-DRC auf Full-Band-Ansicht normalisieren.
 *
 * @param state A800X-DRC-State
 * @param[out] view Normalisierte Full-Band-Ansicht
 * @return ESP_OK bei Erfolg
 */
esp_err_t mvs_drc_a800x_to_view(const mvs_drc_packed_state_t *state,
                                 dsp_drc_view_t *view);

// ---------------------------------------------------------------------------
// Hilfsfunktionen
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// PreEQ-Schema-Adapter
// ---------------------------------------------------------------------------

/**
 * @brief PreEQ-State für das angegebene Schema vorbereiten.
 *
 * Für MVS_PEQ_SCHEMA_CLASSIC_10BAND: selected_filter = Anzahl aktiver Filter
 * Für MVS_PEQ_SCHEMA_A800X: selected_filter unverändert lassen
 *
 * @param schema PreEQ-Schema
 * @param state Zu modifizierender PreEQ-State
 */
void mvs_prepare_preeq_for_schema(mvs_preeq_schema_t schema,
                                  mvs_preeq_state_t *state);

#ifdef __cplusplus
}
#endif
