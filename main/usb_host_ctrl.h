// SPDX-FileCopyrightText: 2026 CobbyCode
// SPDX-License-Identifier: GPL-3.0-or-later
//
// usb_host_ctrl.h — USB-Host-Controller für MVSilicon-Gerät
//
// Verwaltet die USB-Host-Initialisierung, Device-Enumeration
// und HID-SET_REPORT-Transfers.
//
// Unterstützt zwei Transportprofile:
//   A800X:         0x8888:0x171E, Interface 0, 256 Byte
//   Generic Classic: 0x8888:0x1719, Interface 4, 256 Byte
//

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// USB-Transport-Profil
// ---------------------------------------------------------------------------

typedef enum {
    MVS_USB_PROFILE_NONE = 0,
    MVS_USB_PROFILE_A800X,
    MVS_USB_PROFILE_GENERIC_CLASSIC,
} mvs_usb_profile_kind_t;

typedef struct {
    mvs_usb_profile_kind_t kind;
    uint16_t vid;
    uint16_t pid;
    uint8_t interface_number;
    uint16_t report_size;
} mvs_usb_transport_t;

typedef struct {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} mvs_usb_control_setup_t;

// Bekannte Profile
#define MVS_TRANSPORT_A800X \
    ((mvs_usb_transport_t){ \
        .kind = MVS_USB_PROFILE_A800X, \
        .vid = 0x8888, .pid = 0x171E, \
        .interface_number = 0, .report_size = 256 })

#define MVS_TRANSPORT_GENERIC_CLASSIC \
    ((mvs_usb_transport_t){ \
        .kind = MVS_USB_PROFILE_GENERIC_CLASSIC, \
        .vid = 0x8888, .pid = 0x1719, \
        .interface_number = 4, .report_size = 256 })

/**
 * @return Aktives Transportprofil (oder MVS_USB_PROFILE_NONE falls keins geclaimt).
 */
mvs_usb_profile_kind_t usb_host_ctrl_get_profile(void);

/**
 * @return VID des aktiven Transportprofils.
 */
uint16_t usb_host_ctrl_get_vid(void);

/**
 * @return PID des aktiven Transportprofils.
 */
uint16_t usb_host_ctrl_get_pid(void);

/** Copy the active claimed transport. */
esp_err_t usb_host_ctrl_get_transport(mvs_usb_transport_t *transport);

/** Pure profile lookup used by enumeration and host-side regression tests. */
bool usb_host_ctrl_select_transport(uint16_t vid, uint16_t pid,
                                    mvs_usb_transport_t *transport);
void mvs_usb_make_set_report_setup(const mvs_usb_transport_t *transport,
                                   mvs_usb_control_setup_t *setup);
void mvs_usb_make_get_report_setup(const mvs_usb_transport_t *transport,
                                   mvs_usb_control_setup_t *setup);

// ---------------------------------------------------------------------------
// Lebenszyklus
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// HID-Control-Transfers
// ---------------------------------------------------------------------------

/**
 * @brief HID SET_REPORT an das MVSilicon-Gerät senden.
 *
 * Sendet `data` als HID SET_REPORT (report_size Byte, mit Nullen aufgefüllt).
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
