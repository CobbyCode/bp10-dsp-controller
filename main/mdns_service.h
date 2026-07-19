// SPDX-FileCopyrightText: 2026 CobbyCode
// SPDX-License-Identifier: GPL-3.0-or-later
//
// mdns_service.h — mDNS-Dienst
//

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief mDNS-Dienst starten.
 *
 * Registriert den Hostnamen sowie den HTTP-Service (_http._tcp).
 *
 * @param hostname Hostname (z. B. "bp10-xxxx")
 * @return esp_err_t
 */
esp_err_t mdns_service_start(const char *hostname);

/**
 * @brief mDNS-Dienst aktualisieren (z. B. bei Hostname-Änderung).
 *
 * @param hostname Neuer Hostname
 * @return esp_err_t
 */
esp_err_t mdns_service_update_hostname(const char *hostname);

/**
 * @brief mDNS-Dienst stoppen.
 */
void mdns_service_stop(void);

#ifdef __cplusplus
}
#endif
