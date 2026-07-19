// SPDX-FileCopyrightText: 2026 CobbyCode
// SPDX-License-Identifier: GPL-3.0-or-later
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

    mvs_preeq_schema_t preeq_schema;
    mvs_drc_schema_t drc_schema;
} mvs_device_profile_t;

typedef enum {
    MVS_MODULE_NOISE_SUPPRESSOR = 0,
    MVS_MODULE_VIRTUAL_BASS,
    MVS_MODULE_PREEQ,
    MVS_MODULE_DRC,
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
        .preeq_schema = MVS_PEQ_SCHEMA_A800X,                 \
        .drc_schema = MVS_DRC_SCHEMA_A800X_4PATH,             \
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

#ifdef __cplusplus
}
#endif
