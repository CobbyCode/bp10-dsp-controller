// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// app_config.h — Zentrale Board-Konfiguration
//
// Board-spezifische Parameter wie Flash/PSRAM-Größe, VBUS-Schaltung
// und GPIO-Pins. Hier zentral konfigurierbar.
//

#pragma once

#include <stdint.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- USB ---
#define A800X_USB_VID              CONFIG_A800X_USB_VID
#define A800X_USB_PID              CONFIG_A800X_USB_PID
#define A800X_HID_REPORT_SIZE      CONFIG_A800X_HID_REPORT_SIZE
#define A800X_USB_DEVICE_WAIT_MS   CONFIG_A800X_USB_DEVICE_WAIT_MS

// --- WiFi Setup ---
#define A800X_WIFI_SETUP_TIMEOUT_S CONFIG_A800X_WIFI_SETUP_TIMEOUT_S

// --- mDNS ---
#define A800X_MDNS_DEFAULT_PREFIX  CONFIG_A800X_MDNS_DEFAULT_HOSTNAME

// --- HTTP ---
#define A800X_HTTP_PORT            CONFIG_A800X_HTTP_PORT

// --- NVS ---
#define A800X_NVS_NAMESPACE        CONFIG_A800X_NVS_NAMESPACE
#define A800X_MAX_PROFILES         CONFIG_A800X_MAX_PROFILES

// --- GPIO Pins (board-spezifisch, hier anpassbar) ---
//
// Die folgenden GPIO-Definitionen sind für ein typisches ESP32-S3-Board
// mit USB-OTG auf GPIO 20 (DP) und 19 (DM) vorkonfiguriert.
//
// Für andere Boards (z. B. mit integriertem USB-Serial-JTAG) können diese
// Werte überschrieben werden.

#ifndef A800X_GPIO_USB_DP
#define A800X_GPIO_USB_DP          20
#endif
#ifndef A800X_GPIO_USB_DM
#define A800X_GPIO_USB_DM          19
#endif
#ifndef A800X_GPIO_VBUS_ENABLE
#define A800X_GPIO_VBUS_ENABLE     21  // GPIO zum Schalten der VBUS-Versorgung
#endif
#ifndef A800X_GPIO_STATUS_LED
#define A800X_GPIO_STATUS_LED      48  // Eingebaute RGB-LED (WS2812) oder einfache LED
#endif
#ifndef A800X_GPIO_BOOT_BUTTON
#define A800X_GPIO_BOOT_BUTTON      0  // BOOT-Taste (für Factory-Reset)
#endif

// --- VBUS-Konfiguration ---
// Bei einigen Boards muss VBUS (5V für das USB-Gerät) über einen GPIO
// geschaltet werden. Auf 0 setzen, wenn keine Schaltung nötig ist.
#ifndef A800X_VBUS_ENABLE_ACTIVE_HIGH
#define A800X_VBUS_ENABLE_ACTIVE_HIGH 1
#endif

// --- PSRAM ---
// Maximale Größe für Allokationen im externen RAM
#define A800X_PSRAM_ALLOC_SIZE     (4 * 1024 * 1024)  // 4 MB

// --- Task Stack Sizes ---
#define A800X_USB_HOST_TASK_STACK_SIZE  (4096)
#define A800X_WIFI_TASK_STACK_SIZE      (6144)
#define A800X_HTTP_TASK_STACK_SIZE      (8192)

// --- Task Priorities ---
#define A800X_USB_HOST_TASK_PRIORITY    (8)
#define A800X_WIFI_TASK_PRIORITY        (5)
#define A800X_HTTP_TASK_PRIORITY        (5)

#ifdef __cplusplus
}
#endif