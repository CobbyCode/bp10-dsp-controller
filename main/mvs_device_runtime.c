/*
 * SPDX-FileCopyrightText: 2026 CobbyCode
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mvs_device_runtime.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "usb_host_ctrl.h"
#include "mvs_protocol.h"
#include "mvs_device_profile.h"
#include "dsp_model.h"

static const char *TAG = "bp10_device_runtime";
static bool s_ready;

static esp_err_t exchange(const uint8_t *frame, size_t frame_len,
                          uint8_t *response, uint16_t *response_len)
{
    uint8_t report[256];
    mvs_prepare_hid_report(frame, (uint16_t)frame_len, report);
    esp_err_t err = usb_host_ctrl_send_report(report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    return usb_host_ctrl_get_report(response, response_len);
}

static bool response_payload(const uint8_t *response, uint16_t response_len,
                             uint8_t effect_id, uint8_t marker,
                             const uint8_t **payload, uint16_t *payload_len)
{
    if (!response || response_len < 6 || response[0] != MVS_FRAME_MAGIC_1 ||
        response[1] != MVS_FRAME_MAGIC_2 || response[2] != effect_id)
        return false;
    uint8_t wire_len = response[3];
    if (wire_len < 1 || (size_t)wire_len + 5U > response_len ||
        response[4] != marker ||
        response[4U + wire_len] != MVS_FRAME_TERMINATOR)
        return false;
    *payload = response + 5;
    *payload_len = (uint16_t)(wire_len - 1U);
    return true;
}

static esp_err_t read_effect_state(uint8_t effect_id, uint8_t *response,
                                   uint16_t *response_len,
                                   const uint8_t **state, uint16_t *state_len)
{
    uint8_t query[5];
    esp_err_t err = mvs_build_query_frame(effect_id, query, sizeof(query));
    if (err != ESP_OK) return err;
    err = exchange(query, sizeof(query), response, response_len);
    if (err != ESP_OK) return err;
    return response_payload(response, *response_len, effect_id, 0xFF,
                            state, state_len) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t discover_catalog(mvs_device_profile_t *profile,
                                  const mvs_usb_transport_t *transport)
{
    uint8_t request[6];
    uint8_t response[256];
    uint16_t response_len = 0;
    uint16_t types[MVS_CATALOG_MAX_EFFECTS] = {0};
    uint8_t count = 0;

    esp_err_t err = mvs_build_catalog_request_frame(0, request, sizeof(request));
    if (err != ESP_OK) return err;
    err = exchange(request, sizeof(request), response, &response_len);
    if (err != ESP_OK) return err;
    err = mvs_parse_catalog_list(response, response_len, &count, types,
                                 MVS_CATALOG_MAX_EFFECTS);
    if (err != ESP_OK) return err;

    mvs_device_profile_begin_generic(profile, transport->vid, transport->pid,
                                     transport->interface_number, count);
    ESP_LOGI(TAG, "Generic ACP catalog: %u entries", count);

    for (uint8_t index = 1; index <= count; index++) {
        err = mvs_build_catalog_request_frame(index, request, sizeof(request));
        if (err != ESP_OK) return err;
        response_len = 0;
        err = exchange(request, sizeof(request), response, &response_len);
        if (err != ESP_OK) return err;
        if (response_len < 6 || response[0] != MVS_FRAME_MAGIC_1 ||
            response[1] != MVS_FRAME_MAGIC_2 || response[2] != MVS_CATALOG_REQUEST ||
            response[3] < 1 || response[4] != index ||
            (size_t)response[3] + 5U > response_len ||
            response[4U + response[3]] != MVS_FRAME_TERMINATOR) {
            ESP_LOGW(TAG, "Catalog name response %u invalid", index);
            continue;
        }
        char name[64];
        uint16_t raw_name_len = response[3] - 1U;
        mvs_normalize_catalog_name(response + 5, raw_name_len,
                                   name, sizeof(name));
        if (mvs_device_profile_map_catalog_entry(profile, index,
                                                 types[index - 1U], name)) {
            ESP_LOGI(TAG, "Catalog match %u -> 0x%02X type=%u %s", index,
                     0x80U + index, types[index - 1U], name);
        }
    }
    return ESP_OK;
}

static void validate_module(mvs_device_profile_t *profile,
                            mvs_module_kind_t module, mvs_effect_ref_t *effect)
{
    if (!effect || effect->effect_id == 0) return;
    uint8_t response[256];
    uint16_t response_len = 0;
    const uint8_t *state = NULL;
    uint16_t state_len = 0;
    esp_err_t err = read_effect_state(effect->effect_id, response, &response_len,
                                      &state, &state_len);
    bool valid = err == ESP_OK;
    if (valid && module == MVS_MODULE_NOISE_SUPPRESSOR) {
        bool enabled; int16_t threshold; uint16_t ratio, attack, release;
        valid = mvs_decode_noise_suppressor(state, state_len, &enabled, &threshold,
                                            &ratio, &attack, &release) == ESP_OK;
    } else if (valid && module == MVS_MODULE_VIRTUAL_BASS) {
        bool enabled, enhanced; uint16_t cutoff, intensity;
        valid = mvs_decode_virtual_bass(state, state_len, &enabled, &cutoff,
                                        &intensity, &enhanced) == ESP_OK;
    } else if (valid && module == MVS_MODULE_VIRTUAL_BASS_CLASSIC) {
        bool enabled; uint16_t cutoff, intensity;
        valid = mvs_decode_virtual_bass_classic(state, state_len, &enabled,
                                                 &cutoff, &intensity) == ESP_OK;
    } else if (valid && module == MVS_MODULE_PHASE) {
        bool inverted;
        valid = mvs_decode_phase(state, state_len, &inverted) == ESP_OK;
    } else if (valid && module == MVS_MODULE_DELAY_HQ) {
        bool enabled, hq; uint16_t delay;
        valid = mvs_decode_delay(state, state_len, &enabled, &delay, &hq) == ESP_OK;
    } else if (valid && module == MVS_MODULE_PREEQ) {
        mvs_preeq_state_t peq;
        valid = mvs_decode_preeq(state, state_len, &peq) == ESP_OK;
    } else if (valid && module == MVS_MODULE_DRC) {
        if (state_len == 38) {
            mvs_drc_classic_state_t drc;
            valid = mvs_decode_drc_classic(state, state_len, &drc) == ESP_OK;
        } else if (state_len == 54) {
            mvs_drc_packed_state_t drc;
            valid = mvs_decode_drc_a800x(state, state_len, &drc) == ESP_OK;
        } else valid = false;
    }
    mvs_device_profile_set_module_validated(profile, module, valid, state_len);
    ESP_LOGI(TAG, "Module 0x%02X validation: %s (%u bytes) err=%s", effect->effect_id,
             valid ? "ok" : "disabled", state_len,
             err == ESP_OK ? "ok" : "read_fail");
}

void mvs_device_runtime_clear(void)
{
    s_ready = false;
    mvs_device_profile_publish(NULL);
    mvs_device_profile_t empty = {0};
    dsp_model_set_device_profile(&empty);
}

bool mvs_device_runtime_is_ready(void)
{
    return s_ready;
}

esp_err_t mvs_device_runtime_identify(void)
{
    mvs_usb_transport_t transport;
    esp_err_t err = usb_host_ctrl_get_transport(&transport);
    if (err != ESP_OK) return err;
    mvs_device_runtime_clear();

    mvs_device_profile_t profile;
    if (transport.kind == MVS_USB_PROFILE_A800X) {
        mvs_device_profile_set_a800x(&profile);
    } else if (transport.kind == MVS_USB_PROFILE_GENERIC_CLASSIC) {
        err = discover_catalog(&profile, &transport);
        if (err != ESP_OK) return err;
        validate_module(&profile, MVS_MODULE_NOISE_SUPPRESSOR,
                        &profile.noise_suppressor);
        validate_module(&profile, MVS_MODULE_VIRTUAL_BASS,
                        &profile.virtual_bass);
        validate_module(&profile, MVS_MODULE_PREEQ, &profile.preeq);
        validate_module(&profile, MVS_MODULE_DRC, &profile.drc);
        if (profile.virtual_bass_classic.effect_id != 0) {
            validate_module(&profile, MVS_MODULE_VIRTUAL_BASS_CLASSIC,
                            &profile.virtual_bass_classic);
        }
        if (profile.phase.effect_id != 0)
            validate_module(&profile, MVS_MODULE_PHASE, &profile.phase);
        if (profile.delay_hq.effect_id != 0)
            validate_module(&profile, MVS_MODULE_DELAY_HQ, &profile.delay_hq);
        if (profile.usb_out_gain.effect_id != 0)
            validate_module(&profile, MVS_MODULE_USB_OUT_GAIN,
                            &profile.usb_out_gain);
        if (!profile.valid) return ESP_ERR_NOT_SUPPORTED;

        // Schema-Fingerprint berechnen (nur Struktur, keine Adressen)
        mvs_device_profile_compute_fingerprint(&profile);
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }

    mvs_device_profile_publish(&profile);
    dsp_model_set_device_profile(&profile);
    s_ready = true;
    ESP_LOGI(TAG, "DSP profile ready: kind=%d", profile.kind);
    return ESP_OK;
}
