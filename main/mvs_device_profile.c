// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// mvs_device_profile.c — MVSilicon-Geräteprofil
//

#include "mvs_device_profile.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"

// Forward declaration (defined below, called by mvs_device_profile_map_catalog_entry)
bool mvs_device_profile_map_catalog_secondary(mvs_device_profile_t *profile,
                                              uint8_t catalog_index,
                                              uint16_t effect_type,
                                              const char *normalized_name);

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
}

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

bool mvs_device_profile_map_catalog_entry(mvs_device_profile_t *profile,
                                          uint8_t catalog_index,
                                          uint16_t effect_type,
                                          const char *normalized_name)
{
    if (!profile || profile->kind != MVS_DEVICE_GENERIC_ACP ||
        !normalized_name) return false;
    if (effect_type == 5 && strcasecmp(normalized_name, "Music Noise Suppressor") == 0)
        return set_candidate(&profile->noise_suppressor, catalog_index, effect_type);
    if (effect_type == 13 && strcasecmp(normalized_name, "Music Virtual Bass") == 0)
        return set_candidate(&profile->virtual_bass, catalog_index, effect_type);
    if (effect_type == 4 && strcasecmp(normalized_name, "Music Pre EQ") == 0)
        return set_candidate(&profile->preeq, catalog_index, effect_type);
    if (effect_type == 2 && strcasecmp(normalized_name, "Music DRC") == 0)
        return set_candidate(&profile->drc, catalog_index, effect_type);
    return mvs_device_profile_map_catalog_secondary(profile, catalog_index,
                                                    effect_type, normalized_name);
}

bool mvs_device_profile_map_catalog_secondary(mvs_device_profile_t *profile,
                                              uint8_t catalog_index,
                                              uint16_t effect_type,
                                              const char *normalized_name)
{
    if (!profile || profile->kind != MVS_DEVICE_GENERIC_ACP ||
        !normalized_name) return false;
    // VB Classic: gleicher effect_type (13) wie VB, anderer Name
    if (effect_type == 13 &&
        strcasecmp(normalized_name, "Music Virtual Bass Classic") == 0)
        return set_candidate(&profile->virtual_bass_classic, catalog_index,
                             effect_type);
    // Phase
    if (strcasecmp(normalized_name, "Music Phase") == 0)
        return set_candidate(&profile->phase, catalog_index, effect_type);
    // Delay/HQ
    if (strcasecmp(normalized_name, "Music Delay") == 0 ||
        strcasecmp(normalized_name, "Music Delay HQ") == 0)
        return set_candidate(&profile->delay_hq, catalog_index, effect_type);
    // USB Out Gain
    if (strcasecmp(normalized_name, "Music USB Out Gain") == 0 ||
        strcasecmp(normalized_name, "USB Out Gain") == 0)
        return set_candidate(&profile->usb_out_gain, catalog_index, effect_type);
    return false;
}

void mvs_device_profile_set_module_validated(mvs_device_profile_t *profile,
                                             mvs_module_kind_t module,
                                             bool valid,
                                             uint16_t state_size)
{
    if (!profile || profile->kind != MVS_DEVICE_GENERIC_ACP) return;
    mvs_effect_ref_t *effect = NULL;
    switch (module) {
        case MVS_MODULE_NOISE_SUPPRESSOR:
            effect = &profile->noise_suppressor;
            valid = valid && state_size == 10;
            break;
        case MVS_MODULE_VIRTUAL_BASS:
            effect = &profile->virtual_bass;
            valid = valid && state_size == 8;
            break;
        case MVS_MODULE_PREEQ:
            effect = &profile->preeq;
            valid = valid && state_size == 106;
            if (valid) profile->preeq_schema = MVS_PEQ_SCHEMA_CLASSIC_10BAND;
            break;
        case MVS_MODULE_DRC:
            effect = &profile->drc;
            valid = valid && (state_size == 38 || state_size == 54);
            if (valid) profile->drc_schema = state_size == 38
                ? MVS_DRC_SCHEMA_CLASSIC_3BAND : MVS_DRC_SCHEMA_A800X_4PATH;
            break;
        case MVS_MODULE_VIRTUAL_BASS_CLASSIC:
            effect = &profile->virtual_bass_classic;
            valid = valid && state_size == 8;  // gleiche Wire-Größe wie VB
            break;
        case MVS_MODULE_PHASE:
            effect = &profile->phase;
            valid = valid && (state_size >= 2 && state_size <= 4);
            break;
        case MVS_MODULE_DELAY_HQ:
            effect = &profile->delay_hq;
            valid = valid && (state_size >= 6 && state_size <= 10);
            break;
        case MVS_MODULE_USB_OUT_GAIN:
            effect = &profile->usb_out_gain;
            valid = valid && (state_size >= 2 && state_size <= 4);
            break;
        case MVS_MODULE_SILENCE_DETECTOR:
            effect = &profile->silence_detector;
            valid = valid && state_size >= 2;
            break;
        default:
            return;
    }
    if (!effect || effect->effect_id == 0) return;
    effect->available = valid;

    // Capability-Flags aktualisieren
    profile->has_virtual_bass_classic = profile->virtual_bass_classic.available;
    profile->has_phase = profile->phase.available;
    profile->has_delay_hq = profile->delay_hq.available;
    profile->has_usb_out_gain = profile->usb_out_gain.available;

    // Profil-Validität: mindestens ein Kernmodul oder ein erweitertes Modul
    profile->valid = profile->noise_suppressor.available ||
                     profile->virtual_bass.available || profile->preeq.available ||
                     profile->drc.available ||
                     profile->virtual_bass_classic.available ||
                     profile->phase.available || profile->delay_hq.available;
}

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
    if (profile->noise_suppressor.available && profile->noise_suppressor.effect_id == effect_id) return true;
    if (profile->virtual_bass.available && profile->virtual_bass.effect_id == effect_id) return true;
    if (profile->preeq.available && profile->preeq.effect_id == effect_id) return true;
    if (profile->drc.available && profile->drc.effect_id == effect_id) return true;
    if (profile->silence_detector.available && profile->silence_detector.effect_id == effect_id) return true;
    if (profile->virtual_bass_classic.available && profile->virtual_bass_classic.effect_id == effect_id) return true;
    if (profile->phase.available && profile->phase.effect_id == effect_id) return true;
    if (profile->delay_hq.available && profile->delay_hq.effect_id == effect_id) return true;
    if (profile->usb_out_gain.available && profile->usb_out_gain.effect_id == effect_id) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Schema-Fingerprint
// ---------------------------------------------------------------------------

static int cmp_u16(const void *a, const void *b)
{
    uint16_t va = *(const uint16_t *)a;
    uint16_t vb = *(const uint16_t *)b;
    return (va > vb) - (va < vb);
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

    // Modultypen sammeln (effect_type der verfügbaren Module)
    uint16_t types[MVS_FP_MAX_MODULE_TYPES];
    uint8_t count = 0;

    if (profile->noise_suppressor.available && count < MVS_FP_MAX_MODULE_TYPES)
        types[count++] = profile->noise_suppressor.effect_type;
    if (profile->virtual_bass.available && count < MVS_FP_MAX_MODULE_TYPES)
        types[count++] = profile->virtual_bass.effect_type;
    if (profile->preeq.available && count < MVS_FP_MAX_MODULE_TYPES)
        types[count++] = profile->preeq.effect_type;
    if (profile->drc.available && count < MVS_FP_MAX_MODULE_TYPES)
        types[count++] = profile->drc.effect_type;
    if (profile->silence_detector.available && count < MVS_FP_MAX_MODULE_TYPES)
        types[count++] = profile->silence_detector.effect_type > 0
                            ? profile->silence_detector.effect_type : 99;
    if (profile->virtual_bass_classic.available && count < MVS_FP_MAX_MODULE_TYPES)
        types[count++] = profile->virtual_bass_classic.effect_type;
    if (profile->phase.available && count < MVS_FP_MAX_MODULE_TYPES)
        types[count++] = profile->phase.effect_type > 0
                            ? profile->phase.effect_type : 98;
    if (profile->delay_hq.available && count < MVS_FP_MAX_MODULE_TYPES)
        types[count++] = profile->delay_hq.effect_type > 0
                            ? profile->delay_hq.effect_type : 97;
    if (profile->usb_out_gain.available && count < MVS_FP_MAX_MODULE_TYPES)
        types[count++] = profile->usb_out_gain.effect_type > 0
                            ? profile->usb_out_gain.effect_type : 96;

    // Sortieren für deterministischen Fingerprint
    qsort(types, count, sizeof(uint16_t), cmp_u16);

    fp->module_type_count = count;
    memcpy(fp->module_types, types, count * sizeof(uint16_t));

    profile->fingerprint_valid = true;
    ESP_LOGI(TAG, "Fingerprint berechnet: VID=0x%04X PID=0x%04X adapter=%u module_count=%u",
             fp->vid, fp->pid, fp->adapter_kind, fp->module_type_count);
}

bool mvs_fingerprint_equal(const mvs_schema_fingerprint_t *a,
                           const mvs_schema_fingerprint_t *b)
{
    if (!a || !b) return false;
    return memcmp(a, b, sizeof(mvs_schema_fingerprint_t)) == 0;
}

uint32_t mvs_fingerprint_hash(const mvs_schema_fingerprint_t *fp)
{
    // FNV-1a 32-bit
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
