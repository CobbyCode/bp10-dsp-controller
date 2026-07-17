// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// api_handlers.h — REST-API-Endpunkte
//

#pragma once

#include "esp_err.h"
#include "http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief REST-API-Handler am HTTP-Server registrieren.
 *
 * Registriert alle API-Endpunkte unter /api/...
 *
 * @param server HTTP-Server-Handle
 */
void api_handlers_register(http_server_handle_t server);

#ifdef __cplusplus
}
#endif