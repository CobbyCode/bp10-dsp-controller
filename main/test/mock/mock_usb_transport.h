// SPDX-FileCopyrightText: 2026 CobbyCode
// SPDX-License-Identifier: GPL-3.0-or-later
//
// mock_usb_transport.h — Mock-USB-Transport für Tests
//

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mock-USB-Transport initialisieren.
 *
 * Ersetzt die echte USB-Hardware durch eine Puffer-basierte
 * Implementierung für Tests.
 */
void mock_usb_transport_init(void);

/**
 * @brief Mock-USB-Transport deinitialisieren.
 */
void mock_usb_transport_deinit(void);

/**
 * @brief Nächste zu sendende Daten im Mock setzen.
 *
 * Simuliert die Antwort des MVSilicon-Geräts auf einen Befehl.
 *
 * @param data Antwortdaten
 * @param length Datenlänge (max 256)
 */
void mock_usb_set_response(const uint8_t *data, uint16_t length);

/**
 * @brief Letzte gesendete Daten aus dem Mock abrufen.
 *
 * @param[out] data Ausgabepuffer
 * @param[out] length Datenlänge
 */
void mock_usb_get_last_send(uint8_t *data, uint16_t *length);

/**
 * @brief Mock-USB-Transport als Sender registrieren.
 *
 * Überschreibt usb_host_ctrl_send_report für Tests.
 *
 * @return esp_err_t
 */
esp_err_t mock_usb_transport_install(void);

/**
 * @brief Mock-USB-Transport deinstallieren.
 */
void mock_usb_transport_uninstall(void);

#ifdef __cplusplus
}
#endif
