// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// config_io.h — Konfigurations-Import/Export
//

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Konfiguration als JSON-String exportieren.
 *
 * Enthält: DSP-Profil, WiFi-Zugangsdaten, Gerätename.
 *
 * @param[out] json Ausgabe-JSON-String (muss mit free() freigegeben werden)
 * @return esp_err_t
 */
esp_err_t config_io_export(char **json);

/**
 * @brief Konfiguration aus JSON-String importieren.
 *
 * Überschreibt die aktuellen Einstellungen in NVS.
 *
 * @param json JSON-String
 * @return esp_err_t
 */
esp_err_t config_io_import(const char *json);

#ifdef __cplusplus
}
#endif