#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_io.h"
#include "nvs_settings.h"

static mvs_device_profile_t active_device;
static dsp_profile_t runtime_profile;
static dsp_profile_t a800x_saved;
static dsp_profile_t generic_saved;
static bool have_a800x;
static bool have_generic;
static unsigned dsp_writes;
static unsigned nvs_writes;

const mvs_device_profile_t *dsp_model_get_device_profile(void)
{
    return &active_device;
}

void dsp_model_get_profile(dsp_profile_t *profile)
{
    *profile = runtime_profile;
}

bool dsp_model_get_default_profile(dsp_profile_t *profile)
{
    if (active_device.kind != MVS_DEVICE_A800X_FIXED) return false;
    memset(profile, 0, sizeof(*profile));
    return true;
}

esp_err_t nvs_settings_load_a800x_config(dsp_profile_t *profile)
{
    if (!have_a800x) return ESP_ERR_NOT_FOUND;
    *profile = a800x_saved;
    return ESP_OK;
}

esp_err_t nvs_settings_load_generic_config(
    const mvs_schema_fingerprint_t *fingerprint, dsp_profile_t *profile)
{
    if (!have_generic ||
        !mvs_fingerprint_equal(fingerprint, &active_device.schema_fingerprint))
        return ESP_ERR_NOT_FOUND;
    *profile = generic_saved;
    return ESP_OK;
}

static dsp_profile_t sample_profile(unsigned marker)
{
    dsp_profile_t p = {0};
    p.noise_suppressor_enabled = true;
    p.noise_suppressor_threshold_raw = (int16_t)(-5000 + marker);
    p.noise_suppressor_ratio = (uint16_t)(2 + marker);
    p.noise_suppressor_attack_ms = 3;
    p.noise_suppressor_release_ms = 90;
    p.virtual_bass_enabled = true;
    p.virtual_bass_cutoff_hz = (uint16_t)(40 + marker);
    p.virtual_bass_intensity_pct = (uint16_t)(5 + marker);
    p.virtual_bass_enhanced = true;
    p.silence_detector_enabled = true;
    p.preeq.block_enabled = 1;
    p.preeq.pre_gain_raw = 256;
    for (int i = 0; i < 10; ++i) {
        p.preeq.filters[i].enabled = (uint8_t)(i & 1);
        p.preeq.filters[i].type = 2;
        p.preeq.filters[i].frequency_hz = (uint16_t)(100 + i * 100);
        p.preeq.filters[i].q_raw = 1024;
        p.preeq.filters[i].gain_raw = (int16_t)(i * 16);
    }
    p.drc.enabled = 1;
    p.drc.mode = 0;
    for (int i = 0; i < 4; ++i) {
        p.drc.pregains[i] = (uint16_t)(4096 + i);
        p.drc.thresholds[i] = (int16_t)(-500 - i);
        p.drc.ratios[i] = (uint16_t)(100 + i);
        p.drc.attacks[i] = (uint16_t)(2 + i);
        p.drc.releases[i] = (uint16_t)(100 + i);
    }
    p.phase2_extended_valid = true;
    p.phase_invert = (marker & 1U) != 0;
    p.delay_ms = (uint16_t)(20 + marker);
    return p;
}

static void set_a800x(void)
{
    memset(&active_device, 0, sizeof(active_device));
    active_device.valid = true;
    active_device.kind = MVS_DEVICE_A800X_FIXED;
}

static void set_generic(void)
{
    memset(&active_device, 0, sizeof(active_device));
    active_device.valid = true;
    active_device.kind = MVS_DEVICE_GENERIC_ACP;
    active_device.fingerprint_valid = true;
    active_device.schema_fingerprint = (mvs_schema_fingerprint_t) {
        .vid = 0x8888, .pid = 0x1719,
        .adapter_kind = MVS_DEVICE_GENERIC_ACP,
        .module_type_count = 4,
        .module_types = { 5, 13, 23, 42 },
    };
}

static char *replace_once(const char *json, const char *from, const char *to)
{
    const char *at = strstr(json, from);
    assert(at);
    size_t prefix = (size_t)(at - json);
    size_t size = strlen(json) - strlen(from) + strlen(to) + 1;
    char *result = malloc(size);
    assert(result);
    memcpy(result, json, prefix);
    strcpy(result + prefix, to);
    strcpy(result + prefix + strlen(to), at + strlen(from));
    return result;
}

static void test_a800x_roundtrip(void)
{
    set_a800x();
    have_a800x = true;
    a800x_saved = sample_profile(1);
    char *json = NULL;
    dsp_profile_t parsed;
    assert(config_io_export(&json) == ESP_OK);
    assert(strstr(json, "\"format_version\":\t1"));
    assert(strstr(json, "\"device_type\":\t\"a800x\""));
    assert(config_io_parse_import(json, &parsed) == ESP_OK);
    assert(parsed.noise_suppressor_ratio == a800x_saved.noise_suppressor_ratio);
    assert(parsed.preeq.filters[7].frequency_hz ==
           a800x_saved.preeq.filters[7].frequency_hz);
    free(json);
}

static void test_generic_roundtrip_and_fingerprint(void)
{
    set_generic();
    have_generic = true;
    generic_saved = sample_profile(7);
    runtime_profile = generic_saved;
    char *json = NULL;
    dsp_profile_t parsed;
    assert(config_io_export(&json) == ESP_OK);
    assert(strstr(json, "\"device_type\":\t\"generic_acp\""));
    assert(strstr(json, "\"schema_fingerprint\""));

    runtime_profile = sample_profile(20);
    assert(config_io_parse_import(json, &parsed) == ESP_OK);
    assert(parsed.noise_suppressor_ratio == generic_saved.noise_suppressor_ratio);
    assert(parsed.virtual_bass_cutoff_hz == generic_saved.virtual_bass_cutoff_hz);
    assert(parsed.phase2_extended_valid == runtime_profile.phase2_extended_valid);
    assert(parsed.delay_ms == runtime_profile.delay_ms);

    char *bad = replace_once(json, "\"pid\":\t5913", "\"pid\":\t5914");
    unsigned before_dsp = dsp_writes, before_nvs = nvs_writes;
    assert(config_io_parse_import(bad, &parsed) == ESP_ERR_INVALID_STATE);
    assert(dsp_writes == before_dsp && nvs_writes == before_nvs);
    free(bad);
    free(json);
}

static void test_format_version_rejected(void)
{
    set_a800x();
    have_a800x = true;
    a800x_saved = sample_profile(2);
    char *json = NULL;
    dsp_profile_t parsed;
    assert(config_io_export(&json) == ESP_OK);
    char *unknown = replace_once(json, "\"format_version\":\t1",
                                 "\"format_version\":\t2");
    assert(config_io_parse_import(unknown, &parsed) == ESP_ERR_NOT_SUPPORTED);
    assert(config_io_parse_import("{\"schema_version\":1}", &parsed) ==
           ESP_ERR_NOT_SUPPORTED);
    free(unknown);
    free(json);
}

int main(void)
{
    test_a800x_roundtrip();
    test_generic_roundtrip_and_fingerprint();
    test_format_version_rejected();
    puts("config_io_host_tests: PASS");
    return 0;
}
