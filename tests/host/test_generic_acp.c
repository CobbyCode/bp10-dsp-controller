#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mvs_protocol.h"
#include "mvs_device_profile.h"
#include "usb_host_ctrl.h"

static void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void test_transport_profiles(void)
{
    mvs_usb_transport_t t;
    assert(usb_host_ctrl_select_transport(0x8888, 0x171E, &t));
    assert(t.kind == MVS_USB_PROFILE_A800X && t.interface_number == 0 &&
           t.report_size == 256);
    assert(usb_host_ctrl_select_transport(0x8888, 0x1719, &t));
    assert(t.kind == MVS_USB_PROFILE_GENERIC_CLASSIC && t.interface_number == 4 &&
           t.report_size == 256);
    mvs_usb_control_setup_t setup;
    mvs_usb_make_set_report_setup(&t, &setup);
    assert(setup.request_type == 0x21 && setup.request == 0x09 &&
           setup.value == 0x0200 && setup.index == 4 && setup.length == 256);
    mvs_usb_make_get_report_setup(&t, &setup);
    assert(setup.request_type == 0xA1 && setup.request == 0x01 &&
           setup.value == 0x0100 && setup.index == 4 && setup.length == 256);
    assert(!usb_host_ctrl_select_transport(0x1234, 0x5678, &t));
    assert(t.kind == MVS_USB_PROFILE_NONE);
}

static void test_catalog(void)
{
    uint8_t request[6];
    const uint8_t expected0[] = {0xA5,0x5A,0x80,0x01,0x00,0x16};
    const uint8_t expected1[] = {0xA5,0x5A,0x80,0x01,0x01,0x16};
    assert(mvs_build_catalog_request_frame(0, request, sizeof(request)) == ESP_OK);
    assert(memcmp(request, expected0, sizeof(expected0)) == 0);
    assert(mvs_build_catalog_request_frame(1, request, sizeof(request)) == ESP_OK);
    assert(memcmp(request, expected1, sizeof(expected1)) == 0);

    uint8_t fixture[59] = {0xA5,0x5A,0x80,0x36,0x00,0x1A};
    put_u16(fixture + 6 + 0 * 2, 5);
    put_u16(fixture + 6 + 5 * 2, 13);
    put_u16(fixture + 6 + 14 * 2, 2);
    put_u16(fixture + 6 + 16 * 2, 4);
    fixture[58] = 0x16;
    uint16_t types[64];
    uint8_t count = 0;
    assert(mvs_parse_catalog_list(fixture, sizeof(fixture), &count, types, 64) == ESP_OK);
    assert(count == 26 && types[0] == 5 && types[5] == 13 &&
           types[14] == 2 && types[16] == 4);
    fixture[58] = 0;
    assert(mvs_parse_catalog_list(fixture, sizeof(fixture), &count, types, 64) != ESP_OK);

    char normalized[64];
    const uint8_t raw[] = "2:Music Noise Suppressor ";
    mvs_normalize_catalog_name(raw, sizeof(raw) - 1, normalized, sizeof(normalized));
    assert(strcmp(normalized, "Music Noise Suppressor") == 0);

    mvs_device_profile_t p;
    mvs_device_profile_begin_generic(&p, 0x8888, 0x1719, 4, 26);
    assert(mvs_device_profile_map_catalog_entry(&p, 1, 5, "Music Noise Suppressor"));
    assert(mvs_device_profile_map_catalog_entry(&p, 6, 13, "Music Virtual Bass"));
    assert(mvs_device_profile_map_catalog_entry(&p, 15, 2, "Music DRC"));
    assert(mvs_device_profile_map_catalog_entry(&p, 17, 4, "Music Pre EQ"));
    // VB Classic: Phase 1 Mapping (eff_type 13, anderer Name als VB)
    assert(mvs_device_profile_map_catalog_entry(&p, 7, 13, "Music Virtual Bass Classic"));
    assert(p.virtual_bass_classic.effect_id == 0x87);
    assert(mvs_device_profile_map_catalog_entry(&p, 11, 24, "Music Delay"));
    assert(mvs_device_profile_map_catalog_entry(&p, 14, 20, "Music Phase"));
    // Doppelter Aufruf für gleichen Index liefert false (Slot bereits belegt)
    assert(!mvs_device_profile_map_catalog_entry(&p, 7, 13, "Music Virtual Bass Classic"));
    assert(p.noise_suppressor.effect_id == 0x81 && p.virtual_bass.effect_id == 0x86 &&
           p.drc.effect_id == 0x8F && p.preeq.effect_id == 0x91);
    mvs_device_profile_set_module_validated(&p, MVS_MODULE_NOISE_SUPPRESSOR, true, 10);
    mvs_device_profile_set_module_validated(&p, MVS_MODULE_VIRTUAL_BASS, true, 8);
    mvs_device_profile_set_module_validated(&p, MVS_MODULE_DRC, true, 38);
    mvs_device_profile_set_module_validated(&p, MVS_MODULE_PREEQ, true, 106);
    mvs_device_profile_set_module_validated(&p, MVS_MODULE_VIRTUAL_BASS_CLASSIC, true, 6);
    mvs_device_profile_set_module_validated(&p, MVS_MODULE_DELAY_HQ, true, 8);
    mvs_device_profile_set_module_validated(&p, MVS_MODULE_PHASE, true, 4);
    assert(p.valid && p.drc_schema == MVS_DRC_SCHEMA_CLASSIC_3BAND &&
           p.preeq_schema == MVS_PEQ_SCHEMA_CLASSIC_10BAND &&
           !p.silence_detector.available);
    assert(p.has_virtual_bass_classic && p.has_phase && p.has_delay_hq);
    assert(p.virtual_bass_classic.state_size == 6 && p.phase.state_size == 4 &&
           p.delay_hq.state_size == 8);

    uint8_t phase_wire[] = {1,0,1,0};
    bool inverted = false;
    assert(mvs_decode_phase(phase_wire, sizeof(phase_wire), &inverted) == ESP_OK && inverted);
    uint8_t delay_wire[] = {1,0,25,0,25,0,1,0};
    bool delay_enabled = false, hq = false; uint16_t delay_ms = 0;
    assert(mvs_decode_delay(delay_wire, sizeof(delay_wire), &delay_enabled,
                            &delay_ms, &hq) == ESP_OK);
    assert(delay_enabled && delay_ms == 25 && hq);
}

static void test_preeq_and_a800x_regression(void)
{
    mvs_preeq_state_t state = {0};
    state.block_enabled = 1;
    state.pre_gain_raw = -256;
    state.selected_filter = 7;
    state.filters[0].enabled = 1;
    state.filters[2].enabled = 1;
    mvs_preeq_state_t a800x = state;
    mvs_prepare_preeq_for_schema(MVS_PEQ_SCHEMA_A800X, &a800x);
    assert(a800x.selected_filter == 7);
    mvs_preeq_state_t classic = state;
    mvs_prepare_preeq_for_schema(MVS_PEQ_SCHEMA_CLASSIC_10BAND, &classic);
    assert(classic.selected_filter == 2);

    uint8_t generic_frame[112], a800x_frame[112], legacy_frame[112];
    assert(mvs_build_preeq_full_frame_dyn(0x91, &classic, generic_frame,
                                           sizeof(generic_frame)) == ESP_OK);
    assert(memcmp(generic_frame, (uint8_t[]){0xA5,0x5A,0x91,0x6B,0xFF}, 5) == 0);
    assert(mvs_build_preeq_full_frame_dyn(0x99, &a800x, a800x_frame,
                                           sizeof(a800x_frame)) == ESP_OK);
    assert(mvs_build_preeq_full_frame(&a800x, legacy_frame, sizeof(legacy_frame)) == ESP_OK);
    assert(memcmp(a800x_frame, legacy_frame, sizeof(a800x_frame)) == 0);
    assert(memcmp(a800x_frame, (uint8_t[]){0xA5,0x5A,0x99,0x6B,0xFF}, 5) == 0);

    mvs_drc_packed_state_t drc = {0};
    drc.enabled = 1; drc.mode = 0; drc.thresholds[3] = -500;
    drc.ratios[3] = 100; drc.attacks[3] = 2; drc.releases[3] = 800;
    drc.pregains[3] = 5157;
    uint8_t dynamic_drc[60], legacy_drc[60];
    assert(mvs_build_drc_a800x_full_frame(0x9A, &drc, dynamic_drc,
                                          sizeof(dynamic_drc)) == ESP_OK);
    assert(mvs_build_drc_full_frame(&drc, legacy_drc, sizeof(legacy_drc)) == ESP_OK);
    assert(memcmp(dynamic_drc, legacy_drc, sizeof(dynamic_drc)) == 0);
    assert(dynamic_drc[0] == 0xA5 && dynamic_drc[1] == 0x5A &&
           dynamic_drc[2] == 0x9A && dynamic_drc[3] == 0x37 &&
           dynamic_drc[4] == 0xFF && dynamic_drc[59] == 0x16);

    mvs_device_profile_t fixed;
    mvs_device_profile_set_a800x(&fixed);
    assert(fixed.noise_suppressor.effect_id == 0x88 &&
           fixed.silence_detector.effect_id == 0x89 &&
           fixed.virtual_bass.effect_id == 0x97 && fixed.preeq.effect_id == 0x99 &&
           fixed.drc.effect_id == 0x9A && fixed.drc_schema == MVS_DRC_SCHEMA_A800X_4PATH);
}

static void test_classic_drc(void)
{
    uint8_t wire[38] = {0};
    put_u16(wire + 0, 1); put_u16(wire + 2, 200); put_u16(wire + 4, 2);
    put_u16(wire + 6, 724); put_u16(wire + 8, 724);
    put_u16(wire + 10 + 2 * 2, (uint16_t)-2000);
    put_u16(wire + 16 + 2 * 2, 20);
    put_u16(wire + 22 + 2 * 2, 10);
    put_u16(wire + 28 + 2 * 2, 1000);
    put_u16(wire + 34, 5157); put_u16(wire + 36, 4096);
    mvs_drc_classic_state_t state;
    assert(mvs_decode_drc_classic(wire, sizeof(wire), &state) == ESP_OK);
    dsp_drc_view_t view;
    assert(mvs_drc_classic_to_view(&state, &view) == ESP_OK);
    assert(view.valid && view.full_band_supported && view.enabled);
    assert(fabs(view.threshold_db + 20.0) < 0.001 && view.ratio == 20.0 &&
           view.attack_ms == 10 && view.release_ms == 1000 &&
           fabs(view.pregain_db - 2.0) < 0.01);

    uint8_t frame[16]; size_t frame_len = 0;
    const uint16_t ratios[] = {100,100,20};
    const uint8_t expected_ratio[] =
        {0xA5,0x5A,0x8F,0x07,0x05,0x64,0x00,0x64,0x00,0x14,0x00,0x16};
    assert(mvs_build_write_u16_array_frame(0x8F, 5, ratios, 3, frame,
                                            sizeof(frame), &frame_len) == ESP_OK);
    assert(frame_len == sizeof(expected_ratio) &&
           memcmp(frame, expected_ratio, sizeof(expected_ratio)) == 0);
    const uint16_t thresholds[] = {0,0,(uint16_t)-2000};
    const uint8_t expected_threshold[] =
        {0xA5,0x5A,0x8F,0x07,0x04,0x00,0x00,0x00,0x00,0x30,0xF8,0x16};
    assert(mvs_build_write_u16_array_frame(0x8F, 4, thresholds, 3, frame,
                                            sizeof(frame), &frame_len) == ESP_OK);
    assert(memcmp(frame, expected_threshold, sizeof(expected_threshold)) == 0);
}

static void test_schema_fingerprint_ignores_addresses(void)
{
    mvs_device_profile_t a, b;
    mvs_device_profile_begin_generic(&a, 0x8888, 0x1719, 4, 20);
    mvs_device_profile_begin_generic(&b, 0x8888, 0x1719, 4, 20);
    assert(!a.fingerprint_valid && !b.fingerprint_valid);
    a.noise_suppressor=(mvs_effect_ref_t){.available=true,.effect_id=0x81,.effect_type=5};
    a.virtual_bass=(mvs_effect_ref_t){.available=true,.effect_id=0x86,.effect_type=13};
    b.noise_suppressor=(mvs_effect_ref_t){.available=true,.effect_id=0x91,.effect_type=5};
    b.virtual_bass=(mvs_effect_ref_t){.available=true,.effect_id=0xA6,.effect_type=13};
    mvs_device_profile_compute_fingerprint(&a);
    mvs_device_profile_compute_fingerprint(&b);
    assert(a.fingerprint_valid && b.fingerprint_valid);
    assert(mvs_fingerprint_equal(&a.schema_fingerprint,&b.schema_fingerprint));
    char ka[12], kb[12];
    mvs_fingerprint_to_nvs_key(&a.schema_fingerprint,ka,sizeof(ka));
    mvs_fingerprint_to_nvs_key(&b.schema_fingerprint,kb,sizeof(kb));
    assert(strcmp(ka,kb)==0);
}

static void test_runtime_frames_never_use_flash_commands(void)
{
    uint8_t frame[112];
    mvs_preeq_state_t peq={0};
    assert(mvs_build_preeq_full_frame_dyn(0x91,&peq,frame,sizeof(frame))==ESP_OK);
    assert(frame[2]!=MVS_EFFECT_TAG && frame[2]!=MVS_EFFECT_SAVE);
    mvs_drc_packed_state_t drc={0};
    assert(mvs_build_drc_a800x_full_frame(0x9A,&drc,frame,sizeof(frame))==ESP_OK);
    assert(frame[2]!=MVS_EFFECT_TAG && frame[2]!=MVS_EFFECT_SAVE);
}

int main(void)
{
    test_transport_profiles();
    test_catalog();
    test_preeq_and_a800x_regression();
    test_classic_drc();
    test_schema_fingerprint_ignores_addresses();
    test_runtime_frames_never_use_flash_commands();
    puts("generic_acp_host_tests: PASS");
    return 0;
}
