// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// test_protocol.c — Protokoll-Tests
//
// Testet den MVSilicon-Protokoll-Encoder/Decoder mit Mock-USB.
//

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "mvs_protocol.h"
#include "mock_usb_transport.h"

// ---------------------------------------------------------------------------
// Frame-Building Tests
// ---------------------------------------------------------------------------

TEST_CASE("Query frame building", "[protocol]")
{
    uint8_t buf[16];
    esp_err_t err;

    // Noise Suppressor Query
    err = mvs_build_query_frame(MVS_EFFECT_NOISE_SUPPRESSOR, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // A5 5A 88 00 16
    TEST_ASSERT_EQUAL_HEX8(0xA5, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5A, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x88, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x16, buf[4]);

    // Virtual Bass Query
    err = mvs_build_query_frame(MVS_EFFECT_VIRTUAL_BASS, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_HEX8(0x97, buf[2]);
}

TEST_CASE("Write frame building", "[protocol]")
{
    uint8_t buf[16];
    esp_err_t err;

    // Noise Suppressor OFF: A5 5A 88 03 00 00 00 16
    err = mvs_build_write_frame(MVS_EFFECT_NOISE_SUPPRESSOR,
                                MVS_SEL_BLOCK_ENABLE, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_HEX8(0xA5, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5A, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x88, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[4]);  // selector: block enable
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[5]);  // value lo
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[6]);  // value hi
    TEST_ASSERT_EQUAL_HEX8(0x16, buf[7]);  // terminator

    // Noise Suppressor ON: A5 5A 88 03 00 01 00 16
    err = mvs_build_write_frame(MVS_EFFECT_NOISE_SUPPRESSOR,
                                MVS_SEL_BLOCK_ENABLE, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[6]);

    // Virtual Bass OFF: A5 5A 97 03 00 00 00 16
    err = mvs_build_write_frame(MVS_EFFECT_VIRTUAL_BASS,
                                MVS_SEL_BLOCK_ENABLE, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_HEX8(0x97, buf[2]);

    // Threshold -55.00 dB: A5 5A 88 03 01 84 EA 16
    err = mvs_build_write_frame(MVS_EFFECT_NOISE_SUPPRESSOR,
                                0x01, 0xEA84, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_HEX8(0x84, buf[5]);  // -55.00 dB lo
    TEST_ASSERT_EQUAL_HEX8(0xEA, buf[6]);  // -55.00 dB hi
}

TEST_CASE("Virtual Bass write frames", "[protocol]")
{
    uint8_t buf[16];

    // Cutoff 42 Hz: A5 5A 97 03 01 2A 00 16
    mvs_build_write_frame(MVS_EFFECT_VIRTUAL_BASS, 0x01, 42, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_HEX8(0x2A, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[6]);

    // Intensity 10%: A5 5A 97 03 02 0A 00 16
    mvs_build_write_frame(MVS_EFFECT_VIRTUAL_BASS, 0x02, 10, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_HEX8(0x0A, buf[5]);

    // BassEnhanced on: A5 5A 97 03 03 01 00 16
    mvs_build_write_frame(MVS_EFFECT_VIRTUAL_BASS, 0x03, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[5]);
}

// ---------------------------------------------------------------------------
// Decoder Tests
// ---------------------------------------------------------------------------

TEST_CASE("Noise Suppressor decode", "[protocol]")
{
    // Factory state: enabled, -55.00 dB, ratio 4, attack 2ms, release 100ms
    // 01 00 84 EA 04 00 02 00 64 00
    uint8_t data[] = {0x01, 0x00, 0x84, 0xEA, 0x04, 0x00, 0x02, 0x00, 0x64, 0x00};

    bool enabled;
    int16_t threshold;
    uint16_t ratio, attack, release;

    esp_err_t err = mvs_decode_noise_suppressor(data, sizeof(data),
                                                &enabled, &threshold,
                                                &ratio, &attack, &release);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(enabled);
    TEST_ASSERT_EQUAL(-5500, threshold);
    TEST_ASSERT_EQUAL(4, ratio);
    TEST_ASSERT_EQUAL(2, attack);
    TEST_ASSERT_EQUAL(100, release);
}

TEST_CASE("Virtual Bass decode", "[protocol]")
{
    // Block off, cutoff 42, intensity 4%, bass_enhanced on
    // 00 00 2A 00 04 00 01 00
    uint8_t data[] = {0x00, 0x00, 0x2A, 0x00, 0x04, 0x00, 0x01, 0x00};

    bool enabled, enhanced;
    uint16_t cutoff, intensity;

    esp_err_t err = mvs_decode_virtual_bass(data, sizeof(data),
                                            &enabled, &cutoff,
                                            &intensity, &enhanced);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(enabled);
    TEST_ASSERT_EQUAL(42, cutoff);
    TEST_ASSERT_EQUAL(4, intensity);
    TEST_ASSERT_TRUE(enhanced);
}

TEST_CASE("PreEQ state size", "[protocol]")
{
    // Der PreEQ-State muss genau 106 Bytes haben
    mvs_preeq_state_t state;
    TEST_ASSERT_EQUAL(106, sizeof(state));
}

TEST_CASE("DRC packed state size", "[protocol]")
{
    // Der DRC-State muss genau 54 Bytes haben
    mvs_drc_packed_state_t state;
    TEST_ASSERT_EQUAL(54, sizeof(state));
}

TEST_CASE("DRC full frame includes terminator", "[protocol]")
{
    mvs_drc_packed_state_t state = {0};
    uint8_t frame[60] = {0};

    TEST_ASSERT_EQUAL(ESP_OK,
                      mvs_build_drc_full_frame(&state, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_HEX8(MVS_FRAME_MAGIC_1, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(MVS_FRAME_MAGIC_2, frame[1]);
    TEST_ASSERT_EQUAL_HEX8(MVS_EFFECT_DRC, frame[2]);
    TEST_ASSERT_EQUAL_HEX8(0x37, frame[3]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, frame[4]);
    TEST_ASSERT_EQUAL_HEX8(MVS_FRAME_TERMINATOR, frame[59]);
}

// ---------------------------------------------------------------------------
// Frame Validation Tests
// ---------------------------------------------------------------------------

TEST_CASE("Frame validation", "[protocol]")
{
    // Gültiger Frame
    uint8_t good[] = {0xA5, 0x5A, 0x88, 0x00, 0x16};
    TEST_ASSERT_TRUE(mvs_validate_frame(good, sizeof(good)));

    // Ungültiger Frame (falsches Magic)
    uint8_t bad_magic[] = {0x00, 0x00, 0x88, 0x00, 0x16};
    TEST_ASSERT_FALSE(mvs_validate_frame(bad_magic, sizeof(bad_magic)));

    // Zu kurzer Frame
    uint8_t too_short[] = {0xA5, 0x5A};
    TEST_ASSERT_FALSE(mvs_validate_frame(too_short, sizeof(too_short)));
}

// ---------------------------------------------------------------------------
// Tag/Save Frame Tests
// ---------------------------------------------------------------------------

TEST_CASE("Tag frame building", "[protocol]")
{
    uint8_t buf[64];
    esp_err_t err;

    err = mvs_build_tag_frame("Music", buf, sizeof(buf));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    // A5 5A FC 05 4D 75 73 69 63 16
    TEST_ASSERT_EQUAL_HEX8(0xFC, buf[2]);
    TEST_ASSERT_EQUAL_HEX8('M', buf[4]);
    TEST_ASSERT_EQUAL_HEX8('u', buf[5]);
    TEST_ASSERT_EQUAL_HEX8('s', buf[6]);
    TEST_ASSERT_EQUAL_HEX8('i', buf[7]);
    TEST_ASSERT_EQUAL_HEX8('c', buf[8]);
    TEST_ASSERT_EQUAL_HEX8(0x16, buf[9]);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

void test_protocol_main(void)
{
    RUN_TEST("Query frame building", "[protocol]");
    RUN_TEST("Write frame building", "[protocol]");
    RUN_TEST("Virtual Bass write frames", "[protocol]");
    RUN_TEST("Noise Suppressor decode", "[protocol]");
    RUN_TEST("Virtual Bass decode", "[protocol]");
    RUN_TEST("PreEQ state size", "[protocol]");
    RUN_TEST("DRC packed state size", "[protocol]");
    RUN_TEST("DRC full frame includes terminator", "[protocol]");
    RUN_TEST("Frame validation", "[protocol]");
    RUN_TEST("Tag frame building", "[protocol]");
}
