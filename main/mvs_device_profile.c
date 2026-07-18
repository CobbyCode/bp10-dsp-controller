// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// mvs_device_profile.c — MVSilicon-Geräteprofil
//

#include "mvs_device_profile.h"
#include <string.h>
#include <strings.h>
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
        default:
            return;
    }
    if (!effect || effect->effect_id == 0) return;
    effect->available = valid;
    profile->valid = profile->noise_suppressor.available ||
                     profile->virtual_bass.available || profile->preeq.available ||
                     profile->drc.available;
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
    return false;
}
