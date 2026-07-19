// SPDX-FileCopyrightText: 2026 CobbyCode
// SPDX-License-Identifier: GPL-3.0-or-later
//
// config_io.h — DSP-Konfiguration Import/Export (JSON)
//
// Exportiert/importiert NUR die DSP-Konfiguration (keine WiFi/Device-Daten).
// Format ist zwischen ESPs übertragbar.
//

#pragma once

#include "esp_err.h"
#include "dsp_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Schema-Version für Export/Import-Kompatibilität. */
#define DSP_CONFIG_SCHEMA_VERSION 1

/**
 * @brief Aktive DSP-Konfiguration als JSON exportieren.
 *
 * Enthält alle DSP-Module und eine Schema-/Versionsnummer.
 * Keine WiFi-Zugangsdaten, Passwörter, IPs, MAC oder Gerätenamen.
 *
 * @param[out] json Ausgabe-JSON (mit free() freigeben)
 * @return ESP_OK bei Erfolg
 */
esp_err_t config_io_export(char **json);

/**
 * @brief DSP-Konfiguration aus JSON validieren und als dsp_profile_t parsen.
 *
 * Schreibt NICHTS in NVS oder an den DSP – nur Validierung + Parsing.
 * Der Aufrufer entscheidet, ob das Ergebnis angewendet wird.
 *
 * @param json JSON-String
 * @param[out] profile Geparstes Profil (nur bei ESP_OK gültig)
 * @return ESP_OK bei erfolgreicher Validierung
 */
esp_err_t config_io_parse_import(const char *json, dsp_profile_t *profile);

#ifdef __cplusplus
}
#endif
