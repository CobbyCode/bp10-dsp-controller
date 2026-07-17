// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// ota_update.h — OTA-Firmwareupdate
//

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA-Update von einer URL starten.
 *
 * Lädt die Firmware von der angegebenen URL und schreibt sie in
 * die OTA-Partition. Bei Erfolg wird das System neu gestartet.
 *
 * @param url Firmware-URL (http:// oder https://)
 * @return ESP_OK bei Erfolg (System startet neu)
 */
esp_err_t ota_update_start(const char *url);

/**
 * @brief OTA-Update-Status prüfen.
 *
 * @return true wenn ein Update läuft
 */
bool ota_update_is_running(void);

/**
 * @brief Aktuelle Firmware-Version setzen.
 *
 * @param version Versionsstring
 */
void ota_set_version(const char *version);

#ifdef __cplusplus
}
#endif

// APP_VERSION wird in der main.c definiert
#ifndef APP_VERSION
#define APP_VERSION "0.2.0"
#endif