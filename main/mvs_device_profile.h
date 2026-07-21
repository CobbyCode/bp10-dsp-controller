// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// mvs_device_profile.h — MVSilicon-Geräteprofil
//
// Definiert die Geräteprofile (A800X, Generic ACP) mit ihren
// Effekt-IDs, Schemata und Fähigkeiten.
//
// Das A800X-Profil wird ohne Discovery initialisiert (festverdrahtet).
// Das Generic-ACP-Profil wird durch Katalogabfrage (0x80/0x81) aufgebaut.
//

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Gerätetypen
// ---------------------------------------------------------------------------

typedef enum {
    MVS_DEVICE_NONE = 0,
    MVS_DEVICE_A800X_FIXED,
    MVS_DEVICE_GENERIC_ACP,
} mvs_device_kind_t;

// ---------------------------------------------------------------------------
// Schemata
// ---------------------------------------------------------------------------

typedef enum {
    MVS_DRC_SCHEMA_NONE = 0,
    MVS_DRC_SCHEMA_A800X_4PATH,
    MVS_DRC_SCHEMA_CLASSIC_3BAND,
} mvs_drc_schema_t;

typedef enum {
    MVS_PEQ_SCHEMA_NONE = 0,
    MVS_PEQ_SCHEMA_A800X,
    MVS_PEQ_SCHEMA_CLASSIC_10BAND,
} mvs_preeq_schema_t;

// ---------------------------------------------------------------------------
// Effekt-Referenz
// ---------------------------------------------------------------------------

typedef struct {
    bool available;
    uint8_t effect_id;
    uint16_t effect_type;  // ACP-Katalog-Typ (nur für Generic)
} mvs_effect_ref_t;

// ---------------------------------------------------------------------------
// Schema-Fingerprint (Generic)
//
// Stabiler Schlüssel für Generic-Geräte. Beschreibt die STRUKTUR des Geräts
// (VID/PID, Adapter, Modultypen), NICHT die konkreten Effekt-Adressen.
// Adressen dürfen beim Reconnect neu entdeckt werden.
// ---------------------------------------------------------------------------

#define MVS_FP_MAX_MODULE_TYPES 12

typedef struct {
    uint16_t vid;
    uint16_t pid;
    uint8_t  adapter_kind;                          // mvs_usb_profile_kind_t
    uint8_t  module_type_count;                     // Anzahl erkannter Modultypen
    uint16_t module_types[MVS_FP_MAX_MODULE_TYPES]; // Typ-Codes (sortiert)
} mvs_schema_fingerprint_t;  // 28 Bytes

// ---------------------------------------------------------------------------
// Geräteprofil (vollständig)
// ---------------------------------------------------------------------------

typedef struct {
    bool valid;
    mvs_device_kind_t kind;

    uint16_t vid;
    uint16_t pid;
    uint8_t usb_interface;

    bool catalog_discovered;
    uint8_t catalog_count;

    mvs_effect_ref_t noise_suppressor;
    mvs_effect_ref_t virtual_bass;
    mvs_effect_ref_t preeq;
    mvs_effect_ref_t drc;
    mvs_effect_ref_t silence_detector;

    // Optionale/erweiterte Effekte (Phase 2+)
    mvs_effect_ref_t virtual_bass_classic;
    mvs_effect_ref_t phase;
    mvs_effect_ref_t delay_hq;
    mvs_effect_ref_t usb_out_gain;

    // Capability-Flags
    bool has_virtual_bass_classic;
    bool has_phase;
    bool has_delay_hq;
    bool has_usb_out_gain;

    mvs_preeq_schema_t preeq_schema;
    mvs_drc_schema_t drc_schema;

    // Schema-Fingerprint (nur Generic, nach Discovery gesetzt)
    mvs_schema_fingerprint_t schema_fingerprint;
    bool fingerprint_valid;
} mvs_device_profile_t;

typedef enum {
    MVS_MODULE_NOISE_SUPPRESSOR = 0,
    MVS_MODULE_VIRTUAL_BASS,
    MVS_MODULE_PREEQ,
    MVS_MODULE_DRC,
    MVS_MODULE_VIRTUAL_BASS_CLASSIC,
    MVS_MODULE_PHASE,
    MVS_MODULE_DELAY_HQ,
    MVS_MODULE_USB_OUT_GAIN,
    MVS_MODULE_SILENCE_DETECTOR,
} mvs_module_kind_t;

// ---------------------------------------------------------------------------
// A800X-Festprofil-Definition
// ---------------------------------------------------------------------------

#define MVS_A800X_PROFILE                                    \
    ((mvs_device_profile_t){                                 \
        .valid = true,                                       \
        .kind = MVS_DEVICE_A800X_FIXED,                      \
        .vid = 0x8888, .pid = 0x171E, .usb_interface = 0,    \
        .catalog_discovered = false,                          \
        .catalog_count = 0,                                   \
        .noise_suppressor = { .available = true, .effect_id = 0x88 }, \
        .silence_detector = { .available = true, .effect_id = 0x89 }, \
        .virtual_bass = { .available = true, .effect_id = 0x97 },     \
        .preeq = { .available = true, .effect_id = 0x99 },            \
        .drc = { .available = true, .effect_id = 0x9A },              \
        .virtual_bass_classic = { .available = false },                \
        .phase = { .available = false },                               \
        .delay_hq = { .available = false },                            \
        .usb_out_gain = { .available = false },                        \
        .has_virtual_bass_classic = false,                             \
        .has_phase = false,                                            \
        .has_delay_hq = false,                                         \
        .has_usb_out_gain = false,                                     \
        .preeq_schema = MVS_PEQ_SCHEMA_A800X,                 \
        .drc_schema = MVS_DRC_SCHEMA_A800X_4PATH,             \
        .fingerprint_valid = false,                            \
    })

// ---------------------------------------------------------------------------
// Öffentliche API
// ---------------------------------------------------------------------------

/**
 * @brief Geräteprofil initialisieren (leer).
 */
void mvs_device_profile_clear(mvs_device_profile_t *profile);

/**
 * @brief A800X-Festprofil setzen.
 *
 * Wird ohne Discovery initialisiert. Alle bekannten A800X-Effekt-IDs
 * werden direkt gesetzt.
 */
void mvs_device_profile_set_a800x(mvs_device_profile_t *profile);

/** Start a Generic ACP profile before catalog mapping/validation. */
void mvs_device_profile_begin_generic(mvs_device_profile_t *profile,
                                      uint16_t vid, uint16_t pid,
                                      uint8_t usb_interface,
                                      uint8_t catalog_count);

/** Map one exact normalized catalog entry. No substring matching is used. */
bool mvs_device_profile_map_catalog_entry(mvs_device_profile_t *profile,
                                          uint8_t catalog_index,
                                          uint16_t effect_type,
                                          const char *normalized_name);

/** Mark a mapped module available only after its wire layout was validated. */
void mvs_device_profile_set_module_validated(mvs_device_profile_t *profile,
                                             mvs_module_kind_t module,
                                             bool valid,
                                             uint16_t state_size);

/** Publish or clear the process-wide active profile. */
void mvs_device_profile_publish(const mvs_device_profile_t *profile);
const mvs_device_profile_t *mvs_device_profile_get_active(void);

/**
 * @brief Prüfen, ob ein Effekt im Profil verfügbar ist.
 */
bool mvs_device_profile_has_effect(const mvs_device_profile_t *profile,
                                    uint8_t effect_id);

/**
 * @brief Schema-Fingerprint aus dem aktuellen Profil berechnen.
 *
 * Erfasst VID/PID, Adapter-Typ und erkannte Modultypen (sortiert).
 * Effekt-Adressen werden NICHT erfasst – nur die Struktur.
 */
void mvs_device_profile_compute_fingerprint(mvs_device_profile_t *profile);

/**
 * @brief Zwei Fingerprints vergleichen.
 */
bool mvs_fingerprint_equal(const mvs_schema_fingerprint_t *a,
                           const mvs_schema_fingerprint_t *b);

/**
 * @brief Fingerprint-Hash für NVS-Schlüssel berechnen.
 */
uint32_t mvs_fingerprint_hash(const mvs_schema_fingerprint_t *fp);

/**
 * @brief NVS-Schlüssel für Generic-Config ableiten.
 *
 * Format: "dg_" + 8 Hex-Zeichen CRC32.
 * @param fp Fingerprint
 * @param[out] key Ausgabepuffer (mindestens 12 Bytes)
 */
void mvs_fingerprint_to_nvs_key(const mvs_schema_fingerprint_t *fp,
                                 char *key, size_t key_max);

/**
 * @brief Effekt-ID für Noise Suppressor aus Profil holen.
 *
 * @return 0 wenn nicht verfügbar
 */
static inline uint8_t mvs_effect_id_ns(const mvs_device_profile_t *p)
{
    return p->noise_suppressor.available ? p->noise_suppressor.effect_id : 0;
}

/**
 * @brief Effekt-ID für Virtual Bass aus Profil holen.
 */
static inline uint8_t mvs_effect_id_vb(const mvs_device_profile_t *p)
{
    return p->virtual_bass.available ? p->virtual_bass.effect_id : 0;
}

/**
 * @brief Effekt-ID für PreEQ aus Profil holen.
 */
static inline uint8_t mvs_effect_id_preeq(const mvs_device_profile_t *p)
{
    return p->preeq.available ? p->preeq.effect_id : 0;
}

/**
 * @brief Effekt-ID für DRC aus Profil holen.
 */
static inline uint8_t mvs_effect_id_drc(const mvs_device_profile_t *p)
{
    return p->drc.available ? p->drc.effect_id : 0;
}

/**
 * @brief Effekt-ID für Silence Detector aus Profil holen.
 */
static inline uint8_t mvs_effect_id_sd(const mvs_device_profile_t *p)
{
    return p->silence_detector.available ? p->silence_detector.effect_id : 0;
}

static inline uint8_t mvs_effect_id_vb_classic(const mvs_device_profile_t *p)
{
    return p->virtual_bass_classic.available ? p->virtual_bass_classic.effect_id : 0;
}

static inline uint8_t mvs_effect_id_phase(const mvs_device_profile_t *p)
{
    return p->phase.available ? p->phase.effect_id : 0;
}

static inline uint8_t mvs_effect_id_delay_hq(const mvs_device_profile_t *p)
{
    return p->delay_hq.available ? p->delay_hq.effect_id : 0;
}

static inline uint8_t mvs_effect_id_usb_out_gain(const mvs_device_profile_t *p)
{
    return p->usb_out_gain.available ? p->usb_out_gain.effect_id : 0;
}

#ifdef __cplusplus
}
#endif
