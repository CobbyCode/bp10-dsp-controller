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
#include "unity_test_runner.h"
#include "mock_usb_transport.h"

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
    unity_run_menu();
}
