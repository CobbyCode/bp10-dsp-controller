// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// dsp_model.h — DSP-Modell — Zustand und Parameter
//
// Repräsentiert den vollständigen Zustand des MVSilicon-DSP,
// inklusive Noise Suppressor, Virtual Bass, PreEQ, DRC, Silence Detector.
//

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "mvs_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// DSP-Profil (vollständiger Zustand)
// ---------------------------------------------------------------------------

typedef struct {
    // Noise Suppressor (0x88)
    bool    noise_suppressor_enabled;
    int16_t noise_suppressor_threshold_raw; // 0.01 dB
    uint16_t noise_suppressor_ratio;
    uint16_t noise_suppressor_attack_ms;
    uint16_t noise_suppressor_release_ms;

    // Silence Detector (0x89)
    bool silence_detector_enabled;

    // Virtual Bass (0x97)
    bool    virtual_bass_enabled;
    uint16_t virtual_bass_cutoff_hz;
    uint16_t virtual_bass_intensity_pct;
    bool    virtual_bass_enhanced;

    // PreEQ (0x99) — vollständiger State
    mvs_preeq_state_t preeq;

    // DRC / Compressor (0x9A) — vollständiger State
    mvs_drc_packed_state_t drc;

    // Metadaten
    char profile_name[32];
} dsp_profile_t;

// ---------------------------------------------------------------------------
// Öffentliche API
// ---------------------------------------------------------------------------

/**
 * @brief DSP-Modell initialisieren.
 *
 * Lädt das Standard-Profil und bereitet den internen Zustand vor.
 */
esp_err_t dsp_model_init(void);

/**
 * @brief Standard-Profil zurückgeben.
 *
 * @param[out] profile Ausgabe-Profil
 */
void dsp_model_get_default_profile(dsp_profile_t *profile);

/**
 * @brief Ein Profil auf den DSP anwenden (via USB-HID).
 *
 * Sendet die konfigurierten Parameter an den MVSilicon-DSP.
 * Führt KEINEN 0xFD Flash-Save durch.
 *
 * @param profile Zu aktivierendes Profil
 * @return ESP_OK bei Erfolg
 */
esp_err_t dsp_model_apply_profile(const dsp_profile_t *profile);

/**
 * @brief Aktuellen DSP-Zustand vom Gerät lesen (Readback).
 *
 * Führt Readback für alle relevanten Effekte durch.
 *
 * @param[out] profile Ausgabeprofil
 * @return ESP_OK bei Erfolg
 */
esp_err_t dsp_model_readback(dsp_profile_t *profile);

/**
 * @brief Noise Suppressor ein-/ausschalten.
 *
 * @param enable true = an, false = aus
 * @return ESP_OK bei Erfolg
 */
esp_err_t dsp_model_set_noise_suppressor(bool enable);

/**
 * @brief Virtual Bass ein-/ausschalten.
 *
 * @param enable true = an, false = aus
 * @return ESP_OK bei Erfolg
 */
esp_err_t dsp_model_set_virtual_bass(bool enable);

/**
 * @brief Silence Detector ein-/ausschalten.
 *
 * @param enable true = an, false = aus
 * @return ESP_OK bei Erfolg
 */
esp_err_t dsp_model_set_silence_detector(bool enable);

/**
 * @brief PreEQ-Block ein-/ausschalten.
 *
 * @param enable true = an, false = aus
 * @return ESP_OK bei Erfolg
 */
esp_err_t dsp_model_set_preeq_enable(bool enable);

/**
 * @brief DRC-Block ein-/ausschalten.
 *
 * @param enable true = an, false = aus
 * @return ESP_OK bei Erfolg
 */
esp_err_t dsp_model_set_drc_enable(bool enable);

/**
 * @brief Vollständiges PreEQ-State-Update senden.
 *
 * Liest den aktuellen Zustand (Readback), modifiziert die gewünschten
 * Filter und sendet den vollständigen State zurück.
 *
 * @param state Neuer PreEQ-State
 * @return ESP_OK bei Erfolg
 */
esp_err_t dsp_model_update_preeq(const mvs_preeq_state_t *state);

/**
 * @brief Vollständiges DRC-State-Update senden.
 *
 * @param state Neuer DRC-State
 * @return ESP_OK bei Erfolg
 */
esp_err_t dsp_model_update_drc(const mvs_drc_packed_state_t *state);

// ---------------------------------------------------------------------------
// Globaler DSP-Status (definiert in main.c)
// ---------------------------------------------------------------------------
extern bool g_dsp_connected;
extern bool g_dsp_ns_state;

#ifdef __cplusplus
}
#endif