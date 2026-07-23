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
// v2: Multi-Path-Unterstützung (Music / REC)

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
// Pfad-IDs (interne Repräsentation)
// ---------------------------------------------------------------------------

typedef enum {
    MVS_PATH_NONE  = 0,
    MVS_PATH_MUSIC = 1,
    MVS_PATH_REC   = 3,
    MVS_PATH_COUNT = 4,  // interne Slots
} mvs_path_id_t;

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
    uint16_t state_size;   // validierter Readback-State in Bytes
} mvs_effect_ref_t;

// ---------------------------------------------------------------------------
// Pfad (enthält Effekt-Referenzen für einen ACP-List/Pfad)
// ---------------------------------------------------------------------------

typedef struct {
    bool present;             // Pfad ist vom Profil freigegeben
    const char *label;        // UI-Label (z.B. "Music", "REC")
    mvs_path_id_t path_id;    // MVS_PATH_MUSIC / MVS_PATH_REC

    mvs_effect_ref_t noise_suppressor;
    mvs_effect_ref_t virtual_bass;
    mvs_effect_ref_t virtual_bass_classic;
    mvs_effect_ref_t phase;
    mvs_effect_ref_t delay_hq;
    mvs_effect_ref_t preeq;
    mvs_effect_ref_t out_eq;
    mvs_effect_ref_t drc;
    mvs_effect_ref_t usb_out_gain;
    mvs_effect_ref_t silence_detector;

    // Capability-Flags
    bool has_virtual_bass_classic;
    bool has_phase;
    bool has_delay_hq;
    bool has_usb_out_gain;
    bool has_out_eq;

    mvs_preeq_schema_t preeq_schema;
    mvs_preeq_schema_t out_eq_schema;
    mvs_drc_schema_t drc_schema;
} mvs_effect_path_t;

// ---------------------------------------------------------------------------
// Schema-Fingerprint (Generic)
//
// Stabiler Schlüssel für Generic-Geräte. Beschreibt die STRUKTUR des Geräts
// (VID/PID, Adapter, Pfade, Modultypen), NICHT die konkreten Effekt-Adressen.
// Adressen dürfen beim Reconnect neu entdeckt werden.
// ---------------------------------------------------------------------------

#define MVS_FP_MAX_MODULE_TYPES 24

typedef struct {
    uint16_t vid;
    uint16_t pid;
    uint8_t  adapter_kind;                          // mvs_usb_profile_kind_t
    uint8_t  module_type_count;                     // Anzahl erkannter Modultypen
    uint16_t module_types[MVS_FP_MAX_MODULE_TYPES]; // Typ-Codes (sortiert), Pfad+Modul+Schema
} mvs_schema_fingerprint_t;  // 54 Bytes

// ---------------------------------------------------------------------------
// Geräteprofil (vollständig, Multi-Path)
// ---------------------------------------------------------------------------

typedef struct {
    bool valid;
    mvs_device_kind_t kind;

    uint16_t vid;
    uint16_t pid;
    uint8_t usb_interface;

    bool catalog_discovered;
    uint8_t catalog_count;

    // Pfade (Index = mvs_path_id_t)
    mvs_effect_path_t paths[MVS_PATH_COUNT];
    uint8_t path_count;         // Anzahl freigegebener Pfade (1 oder 2)

    // Legacy-Felder (für A800X-Kompatibilität und bestehende Inline-Helper)
    // Diese verweisen auf paths[MVS_PATH_MUSIC].<feld> für Generic.
    // Für A800X werden sie direkt gesetzt.
    mvs_effect_ref_t noise_suppressor;
    mvs_effect_ref_t virtual_bass;
    mvs_effect_ref_t preeq;
    mvs_effect_ref_t drc;
    mvs_effect_ref_t silence_detector;

    mvs_effect_ref_t virtual_bass_classic;
    mvs_effect_ref_t phase;
    mvs_effect_ref_t delay_hq;
    mvs_effect_ref_t usb_out_gain;

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
    MVS_MODULE_OUT_EQ,
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
        .path_count = 1,                                      \
        .paths = {                                           \
            [MVS_PATH_MUSIC] = {                             \
                .present = true, .label = "Music",            \
                .path_id = MVS_PATH_MUSIC,                    \
                .noise_suppressor = { .available = true, .effect_id = 0x88 }, \
                .silence_detector = { .available = true, .effect_id = 0x89 }, \
                .virtual_bass = { .available = true, .effect_id = 0x97 },     \
                .virtual_bass_classic = { .available = true, .effect_id = 0x98 }, \
                .preeq = { .available = true, .effect_id = 0x99 },            \
                .drc = { .available = true, .effect_id = 0x9A },              \
                .phase = { .available = true, .effect_id = 0x96, .state_size = 4 }, \
                .delay_hq = { .available = false },                           \
                .usb_out_gain = { .available = false },                       \
                .out_eq = { .available = false },                             \
                .has_virtual_bass_classic = true,                             \
                .has_phase = true,                                            \
                .has_delay_hq = false,                                        \
                .has_usb_out_gain = false,                                    \
                .has_out_eq = false,                                          \
                .preeq_schema = MVS_PEQ_SCHEMA_A800X,                \
                .drc_schema = MVS_DRC_SCHEMA_A800X_4PATH,            \
            },                                                              \
        },                                                                  \
        .noise_suppressor = { .available = true, .effect_id = 0x88 }, \
        .silence_detector = { .available = true, .effect_id = 0x89 }, \
        .virtual_bass = { .available = true, .effect_id = 0x97 },     \
        .virtual_bass_classic = { .available = true, .effect_id = 0x98 }, \
        .preeq = { .available = true, .effect_id = 0x99 },            \
        .drc = { .available = true, .effect_id = 0x9A },              \
        .phase = { .available = true, .effect_id = 0x96, .state_size = 4 }, \
        .delay_hq = { .available = false },                            \
        .usb_out_gain = { .available = false },                        \
        .has_virtual_bass_classic = true,                              \
        .has_phase = true,                                             \
        .has_delay_hq = false,                                         \
        .has_usb_out_gain = false,                                     \
        .preeq_schema = MVS_PEQ_SCHEMA_A800X,                 \
        .drc_schema = MVS_DRC_SCHEMA_A800X_4PATH,             \
        .fingerprint_valid = false,                            \
    })

// ---------------------------------------------------------------------------
// Öffentliche API
// ---------------------------------------------------------------------------

void mvs_device_profile_clear(mvs_device_profile_t *profile);
void mvs_device_profile_set_a800x(mvs_device_profile_t *profile);

/** Start a Generic ACP profile before catalog mapping/validation. */
void mvs_device_profile_begin_generic(mvs_device_profile_t *profile,
                                      uint16_t vid, uint16_t pid,
                                      uint8_t usb_interface,
                                      uint8_t catalog_count);

/** Map one exact normalized catalog entry. */
bool mvs_device_profile_map_catalog_entry(mvs_device_profile_t *profile,
                                          uint8_t catalog_index,
                                          uint16_t effect_type,
                                          const char *normalized_name);

/** Mark a mapped module available only after its wire layout was validated. */
bool mvs_device_profile_set_module_validated(mvs_device_profile_t *profile,
                                             mvs_path_id_t path_id,
                                             mvs_effect_ref_t *effect_ref,
                                             mvs_module_kind_t module,
                                             bool valid,
                                             uint16_t state_size);

/** Get the effect path for a given path ID (or NULL). */
const mvs_effect_path_t *mvs_device_profile_get_path(
    const mvs_device_profile_t *profile, mvs_path_id_t path_id);

/** Publish or clear the process-wide active profile. */
void mvs_device_profile_publish(const mvs_device_profile_t *profile);
const mvs_device_profile_t *mvs_device_profile_get_active(void);

bool mvs_device_profile_has_effect(const mvs_device_profile_t *profile,
                                    uint8_t effect_id);

/** Compute schema fingerprint including all paths. */
void mvs_device_profile_compute_fingerprint(mvs_device_profile_t *profile);

bool mvs_fingerprint_equal(const mvs_schema_fingerprint_t *a,
                           const mvs_schema_fingerprint_t *b);

uint32_t mvs_fingerprint_hash(const mvs_schema_fingerprint_t *fp);

void mvs_fingerprint_to_nvs_key(const mvs_schema_fingerprint_t *fp,
                                 char *key, size_t key_max);

// ---------------------------------------------------------------------------
// Pfad-basierte Effekt-ID-Inlines
// ---------------------------------------------------------------------------

static inline const mvs_effect_ref_t *mvs_effect_ref_for(
    const mvs_effect_path_t *path, mvs_module_kind_t module)
{
    if (!path) return NULL;
    switch (module) {
        case MVS_MODULE_NOISE_SUPPRESSOR:    return &path->noise_suppressor;
        case MVS_MODULE_VIRTUAL_BASS:        return &path->virtual_bass;
        case MVS_MODULE_PREEQ:               return &path->preeq;
        case MVS_MODULE_DRC:                 return &path->drc;
        case MVS_MODULE_SILENCE_DETECTOR:    return &path->silence_detector;
        case MVS_MODULE_VIRTUAL_BASS_CLASSIC: return &path->virtual_bass_classic;
        case MVS_MODULE_PHASE:               return &path->phase;
        case MVS_MODULE_DELAY_HQ:            return &path->delay_hq;
        case MVS_MODULE_USB_OUT_GAIN:        return &path->usb_out_gain;
        case MVS_MODULE_OUT_EQ:              return &path->out_eq;
        default: return NULL;
    }
}

static inline uint8_t mvs_path_effect_id(
    const mvs_effect_path_t *path, mvs_module_kind_t module)
{
    const mvs_effect_ref_t *ref = mvs_effect_ref_for(path, module);
    return (ref && ref->available) ? ref->effect_id : 0;
}

// Legacy Inlines (A800X-kompatibel, immer Music-Pfad)
static inline uint8_t mvs_effect_id_ns(const mvs_device_profile_t *p)
{
    return p->noise_suppressor.available ? p->noise_suppressor.effect_id : 0;
}

static inline uint8_t mvs_effect_id_vb(const mvs_device_profile_t *p)
{
    return p->virtual_bass.available ? p->virtual_bass.effect_id : 0;
}

static inline uint8_t mvs_effect_id_preeq(const mvs_device_profile_t *p)
{
    return p->preeq.available ? p->preeq.effect_id : 0;
}

static inline uint8_t mvs_effect_id_drc(const mvs_device_profile_t *p)
{
    return p->drc.available ? p->drc.effect_id : 0;
}

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

// ---------------------------------------------------------------------------
// Pfad-String-Konvertierung
// ---------------------------------------------------------------------------

/** Parse "music" or "rec" into mvs_path_id_t. Returns MVS_PATH_NONE on failure. */
mvs_path_id_t mvs_path_from_string(const char *str);

/** Return path label ("Music", "REC") or NULL. */
const char *mvs_path_label(mvs_path_id_t path_id);

#ifdef __cplusplus
}
#endif
