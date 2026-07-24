// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// mvs_device_profile.c — MVSilicon-Geräteprofil
//
// v2: Multi-Path-Unterstützung (Music / REC)

#include "mvs_device_profile.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "bp10_dev_profile";

// Aktives Profil (global)
static mvs_device_profile_t s_active_profile = {0};

// ---------------------------------------------------------------------------
// Öffentliche API
// ---------------------------------------------------------------------------

void mvs_device_profile_clear(mvs_device_profile_t *profile)
{
    if (!profile) return;
    memset(profile, 0, sizeof(*profile));
}

void mvs_device_profile_set_a800x(mvs_device_profile_t *profile)
{
    if (!profile) return;
    *profile = MVS_A800X_PROFILE;
    ESP_LOGI(TAG, "A800X-Festprofil gesetzt (NS:0x%02X VB:0x%02X SD:0x%02X PEQ:0x%02X DRC:0x%02X)",
             profile->noise_suppressor.effect_id,
             profile->virtual_bass.effect_id,
             profile->silence_detector.effect_id,
             profile->preeq.effect_id,
             profile->drc.effect_id);
}

void mvs_device_profile_begin_generic(mvs_device_profile_t *profile,
                                      uint16_t vid, uint16_t pid,
                                      uint8_t usb_interface,
                                      uint8_t catalog_count)
{
    if (!profile) return;
    mvs_device_profile_clear(profile);
    profile->kind = MVS_DEVICE_GENERIC_ACP;
    profile->vid = vid;
    profile->pid = pid;
    profile->usb_interface = usb_interface;
    profile->catalog_discovered = true;
    profile->catalog_count = catalog_count;
    // Pfad-IDs initialisieren (wichtig für set_module_validated-Sync)
    profile->paths[MVS_PATH_MUSIC].path_id = MVS_PATH_MUSIC;
    profile->paths[MVS_PATH_MUSIC].label = "Music";
    profile->paths[MVS_PATH_REC].path_id = MVS_PATH_REC;
    profile->paths[MVS_PATH_REC].label = "Rec";
}

// ---------------------------------------------------------------------------
// Pfad-Helfer
// ---------------------------------------------------------------------------

const mvs_effect_path_t *mvs_device_profile_get_path(
    const mvs_device_profile_t *profile, mvs_path_id_t path_id)
{
    if (!profile || path_id <= MVS_PATH_NONE || path_id >= MVS_PATH_COUNT)
        return NULL;
    if (!profile->paths[path_id].present) return NULL;
    return &profile->paths[path_id];
}

mvs_path_id_t mvs_path_from_string(const char *str)
{
    if (!str) return MVS_PATH_NONE;
    if (strcasecmp(str, "music") == 0) return MVS_PATH_MUSIC;
    if (strcasecmp(str, "rec") == 0)   return MVS_PATH_REC;
    return MVS_PATH_NONE;
}

const char *mvs_path_label(mvs_path_id_t path_id)
{
    switch (path_id) {
        case MVS_PATH_MUSIC: return "Music";
        case MVS_PATH_REC:   return "Rec";
        default:             return NULL;
    }
}

// ---------------------------------------------------------------------------
// Katalog-Mapping
// ---------------------------------------------------------------------------

static bool set_candidate(mvs_effect_ref_t *effect, uint8_t catalog_index,
                          uint16_t effect_type)
{
    if (!effect || catalog_index == 0 || catalog_index > 0x7F ||
        effect->effect_id != 0) return false;
    effect->available = false;
    effect->effect_id = (uint8_t)(0x80U + catalog_index);
    effect->effect_type = effect_type;
    return true;
}

static bool is_rec_prefix(const char *name)
{
    return strncasecmp(name, "Rec ", 4) == 0 ||
           strncasecmp(name, "REC ", 4) == 0;
}

bool mvs_device_profile_map_catalog_entry(mvs_device_profile_t *profile,
                                          uint8_t catalog_index,
                                          uint16_t effect_type,
                                          const char *normalized_name)
{
    if (!profile || profile->kind != MVS_DEVICE_GENERIC_ACP ||
        !normalized_name) return false;

    // REC-Pfad: Module für den Subwoofer-Ausgang
    if (is_rec_prefix(normalized_name)) {
        mvs_effect_path_t *rec = &profile->paths[MVS_PATH_REC];

        if (effect_type == 13 && strcasecmp(normalized_name, "Rec Virtual Bass") == 0)
            return set_candidate(&rec->virtual_bass, catalog_index, effect_type);
        if (strcasecmp(normalized_name, "Rec Virtual Bass Clas") == 0)
            return set_candidate(&rec->virtual_bass_classic, catalog_index, effect_type);
        if (strcasecmp(normalized_name, "Rec Virtual Bass Classic") == 0)
            return set_candidate(&rec->virtual_bass_classic, catalog_index, effect_type);
        if (strcasecmp(normalized_name, "Rec Delay") == 0 ||
            strcasecmp(normalized_name, "Rec Delay HQ") == 0)
            return set_candidate(&rec->delay_hq, catalog_index, effect_type);
        if (effect_type == 2 && strcasecmp(normalized_name, "Rec DRC") == 0)
            return set_candidate(&rec->drc, catalog_index, effect_type);
        if (effect_type == 4 && strcasecmp(normalized_name, "Rec Pre EQ") == 0)
            return set_candidate(&rec->preeq, catalog_index, effect_type);
        if (effect_type == 4 && strcasecmp(normalized_name, "Rec Out EQ") == 0)
            return set_candidate(&rec->out_eq, catalog_index, effect_type);
        return false;
    }

    // Music-Pfad (bestehende Logik)
    if (effect_type == 5 && strcasecmp(normalized_name, "Music Noise Suppressor") == 0)
        return set_candidate(&profile->paths[MVS_PATH_MUSIC].noise_suppressor,
                             catalog_index, effect_type);
    if (effect_type == 13 && strcasecmp(normalized_name, "Music Virtual Bass") == 0)
        return set_candidate(&profile->paths[MVS_PATH_MUSIC].virtual_bass,
                             catalog_index, effect_type);
    if (strcasecmp(normalized_name, "Music Virtual Bass Clas") == 0)
        return set_candidate(&profile->paths[MVS_PATH_MUSIC].virtual_bass_classic,
                             catalog_index, effect_type);
    if (effect_type == 4 && strcasecmp(normalized_name, "Music Pre EQ") == 0)
        return set_candidate(&profile->paths[MVS_PATH_MUSIC].preeq,
                             catalog_index, effect_type);
    if (effect_type == 4 && strcasecmp(normalized_name, "Music Out EQ") == 0)
        return set_candidate(&profile->paths[MVS_PATH_MUSIC].out_eq,
                             catalog_index, effect_type);
    if (effect_type == 2 && strcasecmp(normalized_name, "Music DRC") == 0)
        return set_candidate(&profile->paths[MVS_PATH_MUSIC].drc,
                             catalog_index, effect_type);

    // Music-Secondary
    if (effect_type == 13 &&
        strcasecmp(normalized_name, "Music Virtual Bass Classic") == 0)
        return set_candidate(&profile->paths[MVS_PATH_MUSIC].virtual_bass_classic,
                             catalog_index, effect_type);
    if (strcasecmp(normalized_name, "Music Phase") == 0)
        return set_candidate(&profile->paths[MVS_PATH_MUSIC].phase,
                             catalog_index, effect_type);
    if (strcasecmp(normalized_name, "Music Delay") == 0 ||
        strcasecmp(normalized_name, "Music Delay HQ") == 0)
        return set_candidate(&profile->paths[MVS_PATH_MUSIC].delay_hq,
                             catalog_index, effect_type);
    if (strcasecmp(normalized_name, "Music USB Out Gain") == 0)
        return set_candidate(&profile->paths[MVS_PATH_MUSIC].usb_out_gain,
                             catalog_index, effect_type);
    if (strcasecmp(normalized_name, "USB Out Gain") == 0)
        return set_candidate(&profile->paths[MVS_PATH_REC].usb_out_gain,
                             catalog_index, effect_type);

    return false;
}

// ---------------------------------------------------------------------------
// Modul-Validierung (pfad-unabhängig)
// ---------------------------------------------------------------------------

static bool validate_module_size(mvs_module_kind_t module, uint16_t state_size,
                                  mvs_preeq_schema_t *preeq_schema,
                                  mvs_drc_schema_t *drc_schema)
{
    switch (module) {
        case MVS_MODULE_NOISE_SUPPRESSOR:    return state_size == 10;
        case MVS_MODULE_VIRTUAL_BASS:        return state_size == 8;
        case MVS_MODULE_PREEQ:
            if (preeq_schema) *preeq_schema = MVS_PEQ_SCHEMA_CLASSIC_10BAND;
            return state_size == 106;
        case MVS_MODULE_OUT_EQ:
            if (preeq_schema) *preeq_schema = MVS_PEQ_SCHEMA_CLASSIC_10BAND;
            return state_size == 106;
        case MVS_MODULE_DRC:
            if (state_size == 38) {
                if (drc_schema) *drc_schema = MVS_DRC_SCHEMA_UNIFIED_2BAND;
                return true;
            }
            return false;
        case MVS_MODULE_VIRTUAL_BASS_CLASSIC: return state_size == 6;
        case MVS_MODULE_PHASE:                return (state_size >= 2 && state_size <= 4);
        case MVS_MODULE_DELAY_HQ:             return state_size == 8;
        case MVS_MODULE_USB_OUT_GAIN:         return state_size == 6;
        case MVS_MODULE_SILENCE_DETECTOR:     return state_size >= 2;
        default: return false;
    }
}

bool mvs_device_profile_set_module_validated(mvs_device_profile_t *profile,
                                             mvs_path_id_t path_id,
                                             mvs_effect_ref_t *effect_ref,
                                             mvs_module_kind_t module,
                                             bool valid,
                                             uint16_t state_size)
{
    if (!profile || profile->kind != MVS_DEVICE_GENERIC_ACP ||
        path_id <= MVS_PATH_NONE || path_id >= MVS_PATH_COUNT ||
        !effect_ref) return false;

    mvs_effect_path_t *path = &profile->paths[path_id];
    mvs_effect_ref_t *expected_ref =
        (mvs_effect_ref_t *)mvs_effect_ref_for(path, module);
    if (path->path_id != path_id || expected_ref != effect_ref ||
        effect_ref->effect_id == 0) return false;

    bool size_ok = valid && validate_module_size(module, state_size,
        module == MVS_MODULE_PREEQ ? &path->preeq_schema :
        module == MVS_MODULE_OUT_EQ ? &path->out_eq_schema : NULL,
        module == MVS_MODULE_DRC ? &path->drc_schema : NULL);
    if (valid && !size_ok) valid = false;

    effect_ref->available = valid;
    effect_ref->state_size = valid ? state_size : 0;

    // Capability-Flags pro Pfad aktualisieren
    path->has_virtual_bass_classic = path->virtual_bass_classic.available;
    path->has_phase = path->phase.available;
    path->has_delay_hq = path->delay_hq.available;
    path->has_usb_out_gain = path->usb_out_gain.available;
    path->has_out_eq = path->out_eq.available;

    // Pfad-present setzen wenn mindestens ein Modul validiert ist
    if (valid) path->present = true;

    // Legacy-Felder für A800X/Generic-Kompatibilität (immer Music-Pfad)
    if (path->path_id == MVS_PATH_MUSIC) {
        profile->noise_suppressor = path->noise_suppressor;
        profile->virtual_bass = path->virtual_bass;
        profile->preeq = path->preeq;
        profile->drc = path->drc;
        profile->silence_detector = path->silence_detector;
        profile->virtual_bass_classic = path->virtual_bass_classic;
        profile->phase = path->phase;
        profile->delay_hq = path->delay_hq;
        profile->usb_out_gain = path->usb_out_gain;
        profile->has_virtual_bass_classic = path->has_virtual_bass_classic;
        profile->has_phase = path->has_phase;
        profile->has_delay_hq = path->has_delay_hq;
        profile->has_usb_out_gain = path->has_usb_out_gain;
        profile->preeq_schema = path->preeq_schema;
        profile->drc_schema = path->drc_schema;
    }

    // Profil-Validität: mindestens ein Kernmodul in irgendeinem Pfad
    bool any_valid = false;
    for (int p = 0; p < MVS_PATH_COUNT; p++) {
        const mvs_effect_path_t *pp = &profile->paths[p];
        if (pp->present && (pp->noise_suppressor.available ||
            pp->virtual_bass.available || pp->preeq.available ||
            pp->out_eq.available || pp->drc.available ||
            pp->virtual_bass_classic.available || pp->phase.available ||
            pp->delay_hq.available || pp->usb_out_gain.available)) {
            any_valid = true;
            break;
        }
    }
    profile->valid = any_valid;

    // Zähle freigegebene Pfade
    uint8_t count = 0;
    for (int p = 0; p < MVS_PATH_COUNT; p++) {
        if (profile->paths[p].present) count++;
    }
    profile->path_count = count;
    return true;
}

// ---------------------------------------------------------------------------
// Profil-Publish
// ---------------------------------------------------------------------------

void mvs_device_profile_publish(const mvs_device_profile_t *profile)
{
    if (profile) s_active_profile = *profile;
    else mvs_device_profile_clear(&s_active_profile);
}

const mvs_device_profile_t *mvs_device_profile_get_active(void)
{
    return &s_active_profile;
}

bool mvs_device_profile_has_effect(const mvs_device_profile_t *profile,
                                    uint8_t effect_id)
{
    if (!profile || !profile->valid) return false;
    for (int p = 0; p < MVS_PATH_COUNT; p++) {
        const mvs_effect_path_t *pp = &profile->paths[p];
        if (!pp->present) continue;
        if (pp->noise_suppressor.available && pp->noise_suppressor.effect_id == effect_id) return true;
        if (pp->virtual_bass.available && pp->virtual_bass.effect_id == effect_id) return true;
        if (pp->preeq.available && pp->preeq.effect_id == effect_id) return true;
        if (pp->out_eq.available && pp->out_eq.effect_id == effect_id) return true;
        if (pp->drc.available && pp->drc.effect_id == effect_id) return true;
        if (pp->silence_detector.available && pp->silence_detector.effect_id == effect_id) return true;
        if (pp->virtual_bass_classic.available && pp->virtual_bass_classic.effect_id == effect_id) return true;
        if (pp->phase.available && pp->phase.effect_id == effect_id) return true;
        if (pp->delay_hq.available && pp->delay_hq.effect_id == effect_id) return true;
        if (pp->usb_out_gain.available && pp->usb_out_gain.effect_id == effect_id) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Schema-Fingerprint (Multi-Path)
// ---------------------------------------------------------------------------

static int cmp_u16(const void *a, const void *b)
{
    uint16_t va = *(const uint16_t *)a;
    uint16_t vb = *(const uint16_t *)b;
    return (va > vb) - (va < vb);
}

static void add_fp_entry(uint16_t *types, uint8_t *count,
                         mvs_path_id_t path_id,
                         mvs_module_kind_t module,
                         uint8_t wire_schema,
                         bool available)
{
    if (!available || *count >= MVS_FP_MAX_MODULE_TYPES) return;
    // Stable signature: path + functional module + validated wire schema.
    // Dynamic effect addresses and catalog order are deliberately excluded.
    uint16_t entry = ((uint16_t)(path_id & 0x0F) << 12) |
                     ((uint16_t)(module & 0x0F) << 8) |
                     wire_schema;
    types[(*count)++] = entry;
}

void mvs_device_profile_compute_fingerprint(mvs_device_profile_t *profile)
{
    if (!profile) return;
    memset(&profile->schema_fingerprint, 0, sizeof(profile->schema_fingerprint));
    profile->fingerprint_valid = false;

    mvs_schema_fingerprint_t *fp = &profile->schema_fingerprint;
    fp->vid = profile->vid;
    fp->pid = profile->pid;
    fp->adapter_kind = (uint8_t)profile->kind;

    uint16_t types[MVS_FP_MAX_MODULE_TYPES];
    uint8_t count = 0;

    for (int p = 0; p < MVS_PATH_COUNT; p++) {
        const mvs_effect_path_t *path = &profile->paths[p];
        if (!path->present) continue;

        add_fp_entry(types, &count, path->path_id, MVS_MODULE_NOISE_SUPPRESSOR,
            (uint8_t)path->noise_suppressor.state_size,
            path->noise_suppressor.available);
        add_fp_entry(types, &count, path->path_id, MVS_MODULE_VIRTUAL_BASS,
            (uint8_t)path->virtual_bass.state_size,
            path->virtual_bass.available);
        add_fp_entry(types, &count, path->path_id, MVS_MODULE_PREEQ,
            (uint8_t)path->preeq_schema,
            path->preeq.available);
        add_fp_entry(types, &count, path->path_id, MVS_MODULE_OUT_EQ,
            (uint8_t)path->out_eq_schema,
            path->out_eq.available);
        add_fp_entry(types, &count, path->path_id, MVS_MODULE_DRC,
            (uint8_t)path->drc_schema,
            path->drc.available);
        add_fp_entry(types, &count, path->path_id, MVS_MODULE_SILENCE_DETECTOR,
            (uint8_t)path->silence_detector.state_size,
            path->silence_detector.available);
        add_fp_entry(types, &count, path->path_id, MVS_MODULE_VIRTUAL_BASS_CLASSIC,
            (uint8_t)path->virtual_bass_classic.state_size,
            path->virtual_bass_classic.available);
        add_fp_entry(types, &count, path->path_id, MVS_MODULE_PHASE,
            (uint8_t)path->phase.state_size,
            path->phase.available);
        add_fp_entry(types, &count, path->path_id, MVS_MODULE_DELAY_HQ,
            (uint8_t)path->delay_hq.state_size,
            path->delay_hq.available);
        add_fp_entry(types, &count, path->path_id, MVS_MODULE_USB_OUT_GAIN,
            (uint8_t)path->usb_out_gain.state_size,
            path->usb_out_gain.available);
    }

    qsort(types, count, sizeof(uint16_t), cmp_u16);

    fp->module_type_count = count;
    memcpy(fp->module_types, types, count * sizeof(uint16_t));

    profile->fingerprint_valid = true;
    ESP_LOGI(TAG, "Fingerprint berechnet: VID=0x%04X PID=0x%04X adapter=%u module_count=%u paths=%u",
             fp->vid, fp->pid, fp->adapter_kind, fp->module_type_count, profile->path_count);
}

bool mvs_fingerprint_equal(const mvs_schema_fingerprint_t *a,
                           const mvs_schema_fingerprint_t *b)
{
    if (!a || !b) return false;
    return memcmp(a, b, sizeof(mvs_schema_fingerprint_t)) == 0;
}

uint32_t mvs_fingerprint_hash(const mvs_schema_fingerprint_t *fp)
{
    uint32_t hash = 0x811c9dc5U;
    const uint8_t *data = (const uint8_t *)fp;
    for (size_t i = 0; i < sizeof(mvs_schema_fingerprint_t); i++) {
        hash ^= data[i];
        hash *= 0x01000193U;
    }
    return hash;
}

void mvs_fingerprint_to_nvs_key(const mvs_schema_fingerprint_t *fp,
                                 char *key, size_t key_max)
{
    if (!fp || !key || key_max < 12) { if (key && key_max > 0) key[0] = '\0'; return; }
    uint32_t h = mvs_fingerprint_hash(fp);
    snprintf(key, key_max, "dg_%08lx", (unsigned long)h);
}
