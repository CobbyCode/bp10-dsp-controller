// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// config_io.h — DSP-Konfiguration Import/Export (JSON)
//
// Exportiert/importiert NUR die DSP-Konfiguration (keine WiFi/Device-Daten).
// Format ist zwischen ESPs übertragbar.
//
// v2: Multi-Path-Export (Music + REC getrennt)

#pragma once

#include "esp_err.h"
#include "dsp_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Format-Version (Kompatibilitäts-Gate). */
#define DSP_CONFIG_FORMAT_VERSION 1

/** Schema-Version: 1 = Single-Path, 2 = Multi-Path (Music + REC). */
#define DSP_CONFIG_SCHEMA_VERSION 2

/**
 * @brief Aktive DSP-Konfiguration als JSON exportieren (Multi-Path v2).
 */
esp_err_t config_io_export(char **json);

/**
 * @brief DSP-Konfiguration aus JSON validieren und parsen.
 *
 * Akzeptiert Schema v1 (Single-Path → Music) und v2 (Multi-Path).
 * Schreibt NICHTS in NVS oder an den DSP.
 *
 * @param json JSON-String
 * @param[out] profile Geparstes Profil (Legacy, Music-Pfad)
 * @return ESP_OK bei erfolgreicher Validierung
 */
esp_err_t config_io_parse_import(const char *json, dsp_profile_t *profile);

/**
 * @brief Multi-Path-Import (v2).
 *
 * @param json JSON-String
 * @param[out] config Geparste Multi-Path-Konfiguration
 * @return ESP_OK bei erfolgreicher Validierung
 */
esp_err_t config_io_parse_import_multi(const char *json,
                                        dsp_multi_config_t *config);

#ifdef __cplusplus
}
#endif
