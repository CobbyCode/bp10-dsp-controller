// SPDX-FileCopyrightText: 2026 CobbyCode
// SPDX-License-Identifier: GPL-3.0-or-later
//
// http_server.h — HTTP-Server
//

#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// Typ für Server-Handle (Wrapper um httpd_handle_t)
typedef httpd_handle_t http_server_handle_t;

/**
 * @brief HTTP-Server starten.
 *
 * @param[out] server Server-Handle
 * @param port Port (default 80)
 * @return esp_err_t
 */
esp_err_t http_server_start(http_server_handle_t *server, uint16_t port);

/**
 * @brief HTTP-Server stoppen.
 *
 * @param server Server-Handle
 */
void http_server_stop(http_server_handle_t server);

/**
 * @brief Static-Content-Handler registrieren.
 *
 * Liefert eingebettete Dateien (HTML, CSS, JS) aus.
 *
 * @param server Server-Handle
 */
void http_server_register_static_handlers(http_server_handle_t server);

/**
 * @brief ESP-IDF-Konfiguration für den HTTP-Server zurückgeben.
 *
 * @param port Port
 * @return httpd_config_t
 */
httpd_config_t http_server_get_default_config(uint16_t port);

#ifdef __cplusplus
}
#endif
