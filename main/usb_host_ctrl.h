// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// usb_host_ctrl.h — USB-Host-Controller für MVSilicon-Gerät
//
// Verwaltet die USB-Host-Initialisierung, Device-Enumeration
// und HID-SET_REPORT-Transfers.
//

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief USB-Host-Initialisierung.
 *
 * Startet den USB-Host-Treiber und den Client-Task.
 * Muss vor allen anderen USB-Operationen aufgerufen werden.
 *
 * @return ESP_OK bei Erfolg, sonst Fehlercode.
 */
esp_err_t usb_host_ctrl_init(void);

/**
 * @brief VBUS-Versorgung ein-/ausschalten.
 *
 * @param enable true = VBUS ein, false = VBUS aus
 */
void usb_host_vbus_enable(bool enable);

/**
 * @brief Auf ein bestimmtes USB-Gerät warten.
 *
 * Wartet maximal `timeout_ms` Millisekunden auf ein Gerät
 * mit der angegebenen VID/PID.
 *
 * @param vid Gewünschte Vendor-ID
 * @param pid Gewünschte Product-ID
 * @param timeout_ms Maximale Wartezeit in ms
 * @return ESP_OK bei Erfolg, ESP_ERR_TIMEOUT bei Timeout
 */
esp_err_t usb_host_ctrl_wait_for_device(uint16_t vid, uint16_t pid,
                                        uint32_t timeout_ms);

/**
 * @brief HID SET_REPORT an das MVSilicon-Gerät senden.
 *
 * Sendet `data` als HID SET_REPORT (256 Byte, mit Nullen aufgefüllt).
 *
 * @param data Zeiger auf die Nutzdaten (max. 256 Byte)
 * @param length Länge der Nutzdaten
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t usb_host_ctrl_send_report(const uint8_t *data, uint16_t length);

/**
 * @brief HID GET_REPORT vom MVSilicon-Gerät lesen.
 *
 * Liest ein HID-GET_REPORT (256 Byte) in den bereitgestellten Puffer.
 *
 * @param buffer Ausgabepuffer (mindestens 256 Byte)
 * @param[out] out_length Tatsächliche Länge der gelesenen Daten
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t usb_host_ctrl_get_report(uint8_t *buffer, uint16_t *out_length);

/**
 * @brief Prüfen, ob das MVSilicon-Gerät verbunden ist.
 *
 * @return true wenn verbunden, sonst false
 */
bool usb_host_ctrl_is_device_connected(void);

/**
 * @brief USB-Host herunterfahren.
 */
void usb_host_ctrl_deinit(void);

#ifdef __cplusplus
}
#endif