/*
 * SPDX-FileCopyrightText: 2026 PaulsKlaue
 * SPDX-License-Identifier: MIT
 *
 * app_config.h — Build-time Konfiguration aus Kconfig
 *
 * Zentrale Definition aller Kconfig-abhängigen Konstanten.
 * Keine direkten sdkconfig-Includes in anderen Modulen nötig.
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "sdkconfig.h"

/* ── USB / DSP-Erkennung ─────────────────────────────────── */
#define BP10_USB_VID              CONFIG_BP10_USB_VID
#define BP10_USB_PID              CONFIG_BP10_USB_PID
#define BP10_HID_REPORT_SIZE      CONFIG_BP10_HID_REPORT_SIZE
#define BP10_USB_DEVICE_WAIT_MS   CONFIG_BP10_USB_DEVICE_WAIT_MS

/* ── WiFi / Netzwerk ──────────────────────────────────────── */
#define BP10_WIFI_SETUP_TIMEOUT_S CONFIG_BP10_WIFI_SETUP_TIMEOUT_S

/* ── mDNS ─────────────────────────────────────────────────── */
#define BP10_MDNS_DEFAULT_PREFIX  CONFIG_BP10_MDNS_DEFAULT_HOSTNAME

/* ── HTTP-Server ──────────────────────────────────────────── */
#define BP10_HTTP_PORT            CONFIG_BP10_HTTP_PORT

/* ── NVS / Profile ────────────────────────────────────────── */
#define BP10_NVS_NAMESPACE        CONFIG_BP10_NVS_NAMESPACE
#define BP10_MAX_PROFILES         CONFIG_BP10_MAX_PROFILES

/* ── GPIO / Hardwarekonfiguration ─────────────────────────── */
// USB-OTG-Pins (FPC-Verbindung zum ESP32-S3)
#ifndef BP10_GPIO_USB_DP
#define BP10_GPIO_USB_DP          20
#endif
#ifndef BP10_GPIO_USB_DM
#define BP10_GPIO_USB_DM          19
#endif
#ifndef BP10_GPIO_VBUS_ENABLE
#define BP10_GPIO_VBUS_ENABLE     21  // GPIO zum Schalten der VBUS-Versorgung
#endif
#ifndef BP10_GPIO_STATUS_LED
#define BP10_GPIO_STATUS_LED      48  // Eingebaute RGB-LED (WS2812) oder einfache LED
#endif
#ifndef BP10_GPIO_BOOT_BUTTON
#define BP10_GPIO_BOOT_BUTTON      0  // BOOT-Taste (für Factory-Reset)
#endif

// VBUS-Logikpegel (je nach Transistor-Beschaltung)
#ifndef BP10_VBUS_ENABLE_ACTIVE_HIGH
#define BP10_VBUS_ENABLE_ACTIVE_HIGH 1
#endif

/* ── Speicher / Stack ─────────────────────────────────────── */
#define BP10_PSRAM_ALLOC_SIZE     (4 * 1024 * 1024)  // 4 MB

/* ── Task-Stack-Größen ────────────────────────────────────── */
#define BP10_USB_HOST_TASK_STACK_SIZE  (4096)
#define BP10_WIFI_TASK_STACK_SIZE      (6144)
#define BP10_HTTP_TASK_STACK_SIZE      (8192)

/* ── Task-Prioritäten ──────────────────────────────────────── */
#define BP10_USB_HOST_TASK_PRIORITY    (8)
#define BP10_WIFI_TASK_PRIORITY        (5)
#define BP10_HTTP_TASK_PRIORITY        (5)

#endif /* APP_CONFIG_H */
