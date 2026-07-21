// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// dsp_model.h — DSP-Modell — Zustand und Parameter
//
// Repräsentiert den vollständigen Zustand des MVSilicon-DSP,
// inklusive Noise Suppressor, Virtual Bass, PreEQ, DRC, Silence Detector.
//
// Alle Schreiboperationen verwenden dynamische Effekt-IDs aus dem
// aktiven Geräteprofil (mvs_device_profile.h).
//

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "mvs_protocol.h"
#include "mvs_device_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// DSP-Profil (vollständiger Zustand)
// ---------------------------------------------------------------------------

typedef struct {
    // Noise Suppressor (Profil-NS)
    bool    noise_suppressor_enabled;
    int16_t noise_suppressor_threshold_raw; // 0.01 dB
    uint16_t noise_suppressor_ratio;
    uint16_t noise_suppressor_attack_ms;
    uint16_t noise_suppressor_release_ms;

    // Silence Detector (Profil-SD)
    bool silence_detector_enabled;

    // Virtual Bass (Profil-VB)
    bool    virtual_bass_enabled;
    uint16_t virtual_bass_cutoff_hz;
    uint16_t virtual_bass_intensity_pct;
    bool    virtual_bass_enhanced;

    // PreEQ (Profil-PEQ) — vollständiger State
    mvs_preeq_state_t preeq;

    // DRC / Compressor (Profil-DRC) — vollständiger State
    mvs_drc_packed_state_t drc;

    // Virtual Bass Classic (Phase 2+, separat von VB)
    bool    virtual_bass_classic_enabled;
    uint16_t virtual_bass_classic_cutoff_hz;
    uint16_t virtual_bass_classic_intensity_pct;

    // Phase-Invert (Phase 2+)
    bool    phase_invert;

    // Delay/HQ (Phase 2+)
    bool    delay_hq_enabled;
    uint16_t delay_ms;

    // USB Out Gain (Phase 2+, optional)
    uint16_t usb_out_gain;

    // Appended for backward-safe Phase-2 persistence.
    bool    delay_enabled;
    bool    phase2_extended_valid;
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
 * @brief Geräteprofil setzen.
 *
 * Wird aufgerufen, sobald das USB-Gerät erkannt und das Profil aufgebaut ist.
 * Setzt die Effekt-IDs für alle nachfolgenden Schreib-/Lese-Operationen.
 *
 * @param profile Gültiges Geräteprofil
 */
void dsp_model_set_device_profile(const mvs_device_profile_t *profile);

/**
 * @return Aktuelle Effekt-ID für einen Effekt aus dem Profil, 0 wenn nicht verfügbar.
 */
uint8_t dsp_model_get_effect_id_ns(void);
uint8_t dsp_model_get_effect_id_vb(void);
uint8_t dsp_model_get_effect_id_sd(void);
uint8_t dsp_model_get_effect_id_preeq(void);
uint8_t dsp_model_get_effect_id_drc(void);
uint8_t dsp_model_get_effect_id_vb_classic(void);
uint8_t dsp_model_get_effect_id_phase(void);
uint8_t dsp_model_get_effect_id_delay_hq(void);
uint8_t dsp_model_get_effect_id_usb_out_gain(void);

/**
 * @return Geräteprofil-Referenz.
 */
const mvs_device_profile_t *dsp_model_get_device_profile(void);

/**
 * @brief Standard-Profil zurückgeben.
 *
 * Factory defaults are defined only for the fixed A800X profile. Generic ACP
 * devices deliberately have no synthesized defaults.
 *
 * @param[out] profile Ausgabe-Profil (always cleared first)
 * @return true when A800X defaults were returned, false otherwise
 */
bool dsp_model_get_default_profile(dsp_profile_t *profile);

/**
 * @brief Ein Profil auf den DSP anwenden (via USB-HID).
 *
 * Sendet die konfigurierten Parameter an den MVSilicon-DSP.
 * Führt KEINEN 0xFD Flash-Save durch.
 * Verwendet dynamische Effekt-IDs aus dem aktiven Geräteprofil.
 *
 * @param profile Zu aktivierendes Profil
 * @return ESP_OK bei Erfolg, erster Fehler bei Teilfehlern
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

/** Read and validate the complete PreEQ state. */
esp_err_t dsp_model_read_preeq(mvs_preeq_state_t *state);

/** Read and validate the complete A800X DRC state (54 byte). */
esp_err_t dsp_model_read_drc(mvs_drc_packed_state_t *state);

/** Read the active DRC schema into the normalized Full-Band view. */
esp_err_t dsp_model_read_drc_view(dsp_drc_view_t *view);

/** Schema-aware read/modify/write with complete readback verification. */
esp_err_t dsp_model_update_drc_view(const dsp_drc_view_t *requested,
                                    dsp_drc_view_t *confirmed);

/** Read and validate Silence Detector, including its read-only amplitude. */
esp_err_t dsp_model_read_silence_detector(bool *enabled, uint16_t *amplitude);

/**
 * @brief Enable-Zustand eines Effekts gezielt lesen.
 */
esp_err_t dsp_model_read_effect_enabled(uint8_t effect_id, bool *enabled);

// ---------------------------------------------------------------------------
// Noise Suppressor
// ---------------------------------------------------------------------------

esp_err_t dsp_model_set_noise_suppressor(bool enable);
esp_err_t dsp_model_set_noise_suppressor_state(bool enable,
                                                int16_t threshold_raw,
                                                uint16_t ratio,
                                                uint16_t attack_ms,
                                                uint16_t release_ms);

// ---------------------------------------------------------------------------
// Virtual Bass
// ---------------------------------------------------------------------------

esp_err_t dsp_model_set_virtual_bass(bool enable);
esp_err_t dsp_model_set_virtual_bass_state(bool enable, uint16_t cutoff_hz,
                                            uint16_t intensity_pct,
                                            bool bass_enhanced);

esp_err_t dsp_model_read_virtual_bass_classic(bool *enable, uint16_t *cutoff_hz,
                                               uint16_t *intensity_pct);
esp_err_t dsp_model_set_virtual_bass_classic_state(bool enable,
                                                    uint16_t cutoff_hz,
                                                    uint16_t intensity_pct);
esp_err_t dsp_model_read_phase(bool *phase_invert);
esp_err_t dsp_model_set_phase(bool phase_invert);
esp_err_t dsp_model_read_delay(bool *enable, uint16_t *delay_ms,
                               bool *hq_enabled);
esp_err_t dsp_model_set_delay(bool enable, uint16_t delay_ms,
                              bool hq_enabled);

// ---------------------------------------------------------------------------
// Silence Detector
// ---------------------------------------------------------------------------

esp_err_t dsp_model_set_silence_detector(bool enable);

// ---------------------------------------------------------------------------
// PreEQ
// ---------------------------------------------------------------------------

esp_err_t dsp_model_set_preeq_enable(bool enable);
esp_err_t dsp_model_update_preeq(const mvs_preeq_state_t *state);

// ---------------------------------------------------------------------------
// DRC
// ---------------------------------------------------------------------------

esp_err_t dsp_model_set_drc_enable(bool enable);
esp_err_t dsp_model_update_drc(const mvs_drc_packed_state_t *state);

// ---------------------------------------------------------------------------
// Globaler DSP-Status (definiert in main.c)
// ---------------------------------------------------------------------------
extern bool g_dsp_connected;
extern bool g_dsp_ns_state;

#ifdef __cplusplus
}
#endif
