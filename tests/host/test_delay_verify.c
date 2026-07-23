// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// test_delay_verify.c — Host tests: semantic delay verify comparison
//
// Validates that delay_ms and hq_enabled are only compared when
// delay_enabled == true, matching the fix in dsp_model_verify_full_profile()
// and handler_dsp_delay_post().
//

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    bool    phase2_extended_valid;
    bool    delay_enabled;
    uint16_t delay_ms;
    bool    delay_hq_enabled;
} delay_test_profile_t;

/**
 * @brief Semantic delay comparison mirroring the fixed verify_full_profile.
 *
 * Returns true when the readback matches expectations:
 *  - enabled always compared
 *  - delay_ms / hq_enabled only compared when expected->delay_enabled == true
 */
static bool delay_verify_semantic_ok(const delay_test_profile_t *expected,
                                     const delay_test_profile_t *readback)
{
    if (!expected->phase2_extended_valid) return true; // module not available
    if (readback->delay_enabled != expected->delay_enabled) return false;
    if (expected->delay_enabled) {
        if (readback->delay_ms != expected->delay_ms) return false;
        if (readback->delay_hq_enabled != expected->delay_hq_enabled) return false;
    }
    return true;
}

static void test_off_25ms_hq_off_pass(void)
{
    printf("  OFF / 25 ms / HQ OFF → Verify PASS\n");
    delay_test_profile_t exp = {
        .phase2_extended_valid = true,
        .delay_enabled = false,
        .delay_ms = 25,
        .delay_hq_enabled = false,
    };
    // DSP readback returns enabled=OFF but normalized params
    delay_test_profile_t got = {
        .phase2_extended_valid = true,
        .delay_enabled = false,
        .delay_ms = 0,           // DSP normalized to 0 while OFF
        .delay_hq_enabled = false,
    };
    assert(delay_verify_semantic_ok(&exp, &got));
}

static void test_off_nonstandard_params_pass(void)
{
    printf("  OFF / nonstandard params → PASS\n");
    delay_test_profile_t exp = {
        .phase2_extended_valid = true,
        .delay_enabled = false,
        .delay_ms = 137,          // unusual stored value
        .delay_hq_enabled = true,
    };
    // DSP returns zeros/defaults while OFF
    delay_test_profile_t got = {
        .phase2_extended_valid = true,
        .delay_enabled = false,
        .delay_ms = 0,
        .delay_hq_enabled = false,
    };
    assert(delay_verify_semantic_ok(&exp, &got));
}

static void test_on_correct_params_pass(void)
{
    printf("  ON / correct params → PASS\n");
    delay_test_profile_t exp = {
        .phase2_extended_valid = true,
        .delay_enabled = true,
        .delay_ms = 25,
        .delay_hq_enabled = false,
    };
    delay_test_profile_t got = {
        .phase2_extended_valid = true,
        .delay_enabled = true,
        .delay_ms = 25,
        .delay_hq_enabled = false,
    };
    assert(delay_verify_semantic_ok(&exp, &got));
}

static void test_on_delay_mismatch_fail(void)
{
    printf("  ON / delay_ms mismatch → FAIL\n");
    delay_test_profile_t exp = {
        .phase2_extended_valid = true,
        .delay_enabled = true,
        .delay_ms = 25,
        .delay_hq_enabled = false,
    };
    delay_test_profile_t got = {
        .phase2_extended_valid = true,
        .delay_enabled = true,
        .delay_ms = 50,            // mismatch!
        .delay_hq_enabled = false,
    };
    assert(!delay_verify_semantic_ok(&exp, &got));
}

static void test_on_hq_mismatch_fail(void)
{
    printf("  ON / hq_enabled mismatch → FAIL\n");
    delay_test_profile_t exp = {
        .phase2_extended_valid = true,
        .delay_enabled = true,
        .delay_ms = 25,
        .delay_hq_enabled = false,
    };
    delay_test_profile_t got = {
        .phase2_extended_valid = true,
        .delay_enabled = true,
        .delay_ms = 25,
        .delay_hq_enabled = true,  // mismatch!
    };
    assert(!delay_verify_semantic_ok(&exp, &got));
}

static void test_on_enable_mismatch_fail(void)
{
    printf("  ON / enabled mismatch → FAIL\n");
    delay_test_profile_t exp = {
        .phase2_extended_valid = true,
        .delay_enabled = true,
        .delay_ms = 25,
        .delay_hq_enabled = false,
    };
    delay_test_profile_t got = {
        .phase2_extended_valid = true,
        .delay_enabled = false,     // mismatch!
        .delay_ms = 25,
        .delay_hq_enabled = false,
    };
    assert(!delay_verify_semantic_ok(&exp, &got));
}

static void test_off_enabled_mismatch_fail(void)
{
    printf("  OFF / enabled mismatch → FAIL\n");
    delay_test_profile_t exp = {
        .phase2_extended_valid = true,
        .delay_enabled = false,
        .delay_ms = 25,
        .delay_hq_enabled = false,
    };
    delay_test_profile_t got = {
        .phase2_extended_valid = true,
        .delay_enabled = true,      // mismatch!
        .delay_ms = 25,
        .delay_hq_enabled = false,
    };
    assert(!delay_verify_semantic_ok(&exp, &got));
}

int main(void)
{
    puts("=== delay_verify_host_tests ===");
    test_off_25ms_hq_off_pass();
    test_off_nonstandard_params_pass();
    test_on_correct_params_pass();
    test_on_delay_mismatch_fail();
    test_on_hq_mismatch_fail();
    test_on_enable_mismatch_fail();
    test_off_enabled_mismatch_fail();
    puts("delay_verify_host_tests: PASS");
    return 0;
}
