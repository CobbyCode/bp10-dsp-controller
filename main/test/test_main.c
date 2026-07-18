// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// test_main.c — Test-Einstiegspunkt
//
// Test-Suite für das BP10 DSP Controller Projekt.
// Verwendet das ESP-IDF-Test-Framework (Unity).
//

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "mock_usb_transport.h"

// ---------------------------------------------------------------------------
// Test-Gruppen (deklariert in test_*.c)
// ---------------------------------------------------------------------------
extern void test_protocol_main(void);
extern void test_dsp_model_main(void);

// ---------------------------------------------------------------------------
// Setup/Teardown
// ---------------------------------------------------------------------------

void setUp(void)
{
    mock_usb_transport_install();
}

void tearDown(void)
{
    mock_usb_transport_uninstall();
}

// ---------------------------------------------------------------------------
// Einstieg
// ---------------------------------------------------------------------------

void app_main(void)
{
    UNITY_BEGIN();

    // Protokoll-Tests
    test_protocol_main();

    // DSP-Modell-Tests
    test_dsp_model_main();

    UNITY_END();
}