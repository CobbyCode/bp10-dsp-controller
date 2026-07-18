// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// test_dsp_model.c — DSP-Modell-Tests
//
// Testet das DSP-Modell mit Mock-USB-Transport.
//

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "unity_test_runner.h"
#include "dsp_model.h"
#include "mvs_protocol.h"
#include "mock_usb_transport.h"

// ---------------------------------------------------------------------------
// DSP-Modell Tests
// ---------------------------------------------------------------------------

TEST_CASE("Default profile values", "[dsp_model]")
{
    dsp_profile_t profile;
    dsp_model_get_default_profile(&profile);

    // Noise Suppressor: Factory Defaults
    TEST_ASSERT_TRUE(profile.noise_suppressor_enabled);
    TEST_ASSERT_EQUAL(-5500, profile.noise_suppressor_threshold_raw);
    TEST_ASSERT_EQUAL(4, profile.noise_suppressor_ratio);
    TEST_ASSERT_EQUAL(2, profile.noise_suppressor_attack_ms);
    TEST_ASSERT_EQUAL(100, profile.noise_suppressor_release_ms);

    // Virtual Bass factory state
    TEST_ASSERT_TRUE(profile.virtual_bass_enabled);
    TEST_ASSERT_EQUAL(42, profile.virtual_bass_cutoff_hz);
    TEST_ASSERT_EQUAL(4, profile.virtual_bass_intensity_pct);
    TEST_ASSERT_TRUE(profile.virtual_bass_enhanced);

    // PreEQ: Factory Defaults
    TEST_ASSERT_TRUE(profile.preeq.block_enabled);
    TEST_ASSERT_EQUAL(0, profile.preeq.pre_gain_raw);
    TEST_ASSERT_EQUAL(0, profile.preeq.selected_filter);

    // F0: LP 280 Hz
    TEST_ASSERT_TRUE(profile.preeq.filters[0].enabled);
    TEST_ASSERT_EQUAL(MVS_FILTER_LP, profile.preeq.filters[0].type);
    TEST_ASSERT_EQUAL(280, profile.preeq.filters[0].frequency_hz);
    TEST_ASSERT_EQUAL(1229, profile.preeq.filters[0].q_raw);  // Q ~1.2
    TEST_ASSERT_EQUAL(0, profile.preeq.filters[0].gain_raw);
}

TEST_CASE("DRC factory defaults", "[dsp_model]")
{
    dsp_profile_t profile;
    dsp_model_get_default_profile(&profile);

    // Full Band mode
    TEST_ASSERT_TRUE(profile.drc.enabled);
    TEST_ASSERT_EQUAL(0, profile.drc.mode);

    // Full Band path (index 3): -5.00 dB, 1:1, 2 ms, 800 ms
    TEST_ASSERT_EQUAL(-500, profile.drc.thresholds[3]);
    TEST_ASSERT_EQUAL(100, profile.drc.ratios[3]);
    TEST_ASSERT_EQUAL(2, profile.drc.attacks[3]);
    TEST_ASSERT_EQUAL(800, profile.drc.releases[3]);

    // Inaktive Bänder: 0 dB, 1:1
    TEST_ASSERT_EQUAL(0, profile.drc.thresholds[0]);
    TEST_ASSERT_EQUAL(100, profile.drc.ratios[0]);
}

TEST_CASE("PreEQ filter types", "[dsp_model]")
{
    dsp_profile_t profile;
    dsp_model_get_default_profile(&profile);

    // Verify all filter types
    TEST_ASSERT_EQUAL(MVS_FILTER_LP, profile.preeq.filters[0].type);  // F0: LP
    TEST_ASSERT_EQUAL(MVS_FILTER_LP, profile.preeq.filters[1].type);  // F1: LP
    TEST_ASSERT_EQUAL(MVS_FILTER_HP, profile.preeq.filters[2].type);  // F2: HP
    TEST_ASSERT_EQUAL(MVS_FILTER_PK, profile.preeq.filters[3].type);  // F3: PK
    TEST_ASSERT_EQUAL(MVS_FILTER_PK, profile.preeq.filters[4].type);  // F4: PK
}

// ---------------------------------------------------------------------------
// Minimal known-good commands validation
// ---------------------------------------------------------------------------

TEST_CASE("Minimal known-good commands", "[dsp_model]")
{
    uint8_t frame[16];
    uint8_t expected[8][8] = {
        {0xA5, 0x5A, 0x88, 0x03, 0x00, 0x00, 0x00, 0x16},  // Noise Suppressor off
        {0xA5, 0x5A, 0x97, 0x03, 0x00, 0x00, 0x00, 0x16},  // Virtual Bass off
        {0xA5, 0x5A, 0x89, 0x03, 0x00, 0x00, 0x00, 0x16},  // Silence Detector off
        {0xA5, 0x5A, 0x99, 0x03, 0x00, 0x00, 0x00, 0x16},  // PreEQ off
        {0xA5, 0x5A, 0x9A, 0x03, 0x00, 0x00, 0x00, 0x16},  // DRC off
    };
    uint8_t effects[] = {
        MVS_EFFECT_NOISE_SUPPRESSOR,
        MVS_EFFECT_VIRTUAL_BASS,
        MVS_EFFECT_SILENCE_DETECTOR,
        MVS_EFFECT_PREEQ,
        MVS_EFFECT_DRC,
    };

    for (int i = 0; i < 5; i++) {
        mvs_build_write_frame(effects[i], MVS_SEL_BLOCK_ENABLE, 0,
                              frame, sizeof(frame));
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expected[i], frame, 8,
                                         "Known-good command mismatch");
    }
}
