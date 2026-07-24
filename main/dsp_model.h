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
// v2: Multi-Path-Unterstützung (Music / REC)

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
// DSP-Profil (vollständiger Zustand, ein Pfad)
// ---------------------------------------------------------------------------

typedef struct {
    // Noise Suppressor
    bool    noise_suppressor_enabled;
    int16_t noise_suppressor_threshold_raw;
    uint16_t noise_suppressor_ratio;
    uint16_t noise_suppressor_attack_ms;
    uint16_t noise_suppressor_release_ms;

    // Silence Detector
    bool silence_detector_enabled;

    // Virtual Bass
    bool    virtual_bass_enabled;
    uint16_t virtual_bass_cutoff_hz;
    uint16_t virtual_bass_intensity_pct;
    bool    virtual_bass_enhanced;

    // PreEQ
    mvs_preeq_state_t preeq;

    // Out EQ (Out EQ ist eigenständig, nicht mit PreEQ identisch)
    mvs_preeq_state_t out_eq;

    // DRC
    mvs_drc_packed_state_t drc;

    // Virtual Bass Classic
    bool    virtual_bass_classic_enabled;
    uint16_t virtual_bass_classic_cutoff_hz;
    uint16_t virtual_bass_classic_intensity_pct;

    // Phase-Invert
    bool    phase_invert;

    // Delay/HQ
    bool    delay_hq_enabled;
    uint16_t delay_ms;

    // USB Out Gain
    uint16_t usb_out_gain;

    // Backward-compat
    bool    delay_enabled;
    bool    phase2_extended_valid;

    // Out EQ valid flag
    bool    out_eq_valid;

    // Laufzeitstatus des letzten DRC-Readbacks.
    bool    drc_readback_valid;
} dsp_profile_t;

// ---------------------------------------------------------------------------
// DSP-Multi-Path-Konfiguration (beide Pfade gemeinsam)
// ---------------------------------------------------------------------------

typedef struct {
    uint16_t schema_version;
    dsp_profile_t music;
    dsp_profile_t rec;
    bool rec_valid;  // REC-Pfad hat gespeicherte Konfiguration
} dsp_multi_config_t;

// ---------------------------------------------------------------------------
// Öffentliche API
// ---------------------------------------------------------------------------

esp_err_t dsp_model_init(void);
void dsp_model_set_device_profile(const mvs_device_profile_t *profile);

/** Aktuelles Profil in lokale Kopie (nur Music-Pfad, Legacy). */
void dsp_model_get_profile(dsp_profile_t *out);

/** Multi-Path-Konfiguration aus internem Zustand lesen. */
void dsp_model_get_multi_config(dsp_multi_config_t *out);

/** Lokale Profilkopie atomar als s_current_profile übernehmen (Legacy). */
void dsp_model_commit_profile(const dsp_profile_t *profile);

/** Multi-Path-Konfiguration atomar übernehmen. */
void dsp_model_commit_multi_config(const dsp_multi_config_t *config);

/** Commit nur eines Pfads (lässt den anderen unverändert). */
void dsp_model_commit_path_profile(mvs_path_id_t path_id,
                                    const dsp_profile_t *profile);

/** Legacy Effekt-ID-Zugriff (immer Music-Pfad). */
uint8_t dsp_model_get_effect_id_ns(void);
uint8_t dsp_model_get_effect_id_vb(void);
uint8_t dsp_model_get_effect_id_sd(void);
uint8_t dsp_model_get_effect_id_preeq(void);
uint8_t dsp_model_get_effect_id_drc(void);
uint8_t dsp_model_get_effect_id_vb_classic(void);
uint8_t dsp_model_get_effect_id_phase(void);
uint8_t dsp_model_get_effect_id_delay_hq(void);
uint8_t dsp_model_get_effect_id_usb_out_gain(void);

/** Pfad-basierte Effekt-ID-Abfrage. */
uint8_t dsp_model_get_path_effect_id(mvs_path_id_t path_id,
                                      mvs_module_kind_t module);

const mvs_device_profile_t *dsp_model_get_device_profile(void);

bool dsp_model_get_default_profile(dsp_profile_t *profile);

esp_err_t dsp_model_apply_profile(const dsp_profile_t *profile);

/** Apply multi-path config (all paths, all modules). */
esp_err_t dsp_model_apply_multi_config(const dsp_multi_config_t *config);

/** Apply single path profile to DSP. */
esp_err_t dsp_model_apply_path_profile(mvs_path_id_t path_id,
                                        const dsp_profile_t *profile);

esp_err_t dsp_model_readback(dsp_profile_t *profile);

/** Readback complete multi-path config from DSP. */
esp_err_t dsp_model_readback_multi(dsp_multi_config_t *config);

/** Readback single path from DSP. */
esp_err_t dsp_model_readback_path(mvs_path_id_t path_id,
                                   dsp_profile_t *profile);

esp_err_t dsp_model_read_preeq(mvs_preeq_state_t *state);
esp_err_t dsp_model_read_outeq(mvs_preeq_state_t *state);
esp_err_t dsp_model_read_drc(mvs_drc_packed_state_t *state);
esp_err_t dsp_model_read_drc_view(dsp_drc_view_t *view);
esp_err_t dsp_model_update_drc_view(const dsp_drc_view_t *requested,
                                    dsp_drc_view_t *confirmed);
esp_err_t dsp_model_read_silence_detector(bool *enabled, uint16_t *amplitude);
esp_err_t dsp_model_read_effect_enabled(uint8_t effect_id, bool *enabled);

// ---------------------------------------------------------------------------
// Pfad-basierte Modul-API
// ---------------------------------------------------------------------------

// Noise Suppressor
esp_err_t dsp_model_set_noise_suppressor(bool enable);
esp_err_t dsp_model_set_noise_suppressor_state(bool enable,
    int16_t threshold_raw, uint16_t ratio, uint16_t attack_ms, uint16_t release_ms);
esp_err_t dsp_model_read_noise_suppressor(bool *enabled, int16_t *threshold_raw,
    uint16_t *ratio, uint16_t *attack_ms, uint16_t *release_ms);

// Virtual Bass
esp_err_t dsp_model_set_virtual_bass(bool enable);
esp_err_t dsp_model_set_virtual_bass_state(bool enable, uint16_t cutoff_hz,
    uint16_t intensity_pct, bool bass_enhanced);
esp_err_t dsp_model_read_virtual_bass(bool *enabled, uint16_t *cutoff_hz,
    uint16_t *intensity_pct, bool *enhanced);

// Virtual Bass (path-aware)
esp_err_t dsp_model_set_virtual_bass_path(mvs_path_id_t path_id, bool enable,
    uint16_t cutoff_hz, uint16_t intensity_pct, bool bass_enhanced);
esp_err_t dsp_model_read_virtual_bass_path(mvs_path_id_t path_id,
    bool *enabled, uint16_t *cutoff_hz, uint16_t *intensity_pct, bool *enhanced);

// Virtual Bass Classic (path-aware)
esp_err_t dsp_model_read_virtual_bass_classic(bool *enable, uint16_t *cutoff_hz,
    uint16_t *intensity_pct);
esp_err_t dsp_model_set_virtual_bass_classic_state(bool enable,
    uint16_t cutoff_hz, uint16_t intensity_pct);
esp_err_t dsp_model_set_virtual_bass_classic_path(mvs_path_id_t path,
    bool enable, uint16_t cutoff_hz, uint16_t intensity_pct);
esp_err_t dsp_model_read_virtual_bass_classic_path(mvs_path_id_t path,
    bool *enable, uint16_t *cutoff_hz, uint16_t *intensity_pct);

// Phase
esp_err_t dsp_model_read_phase(bool *phase_invert);
esp_err_t dsp_model_set_phase(bool phase_invert);

// Delay
esp_err_t dsp_model_read_delay(bool *enable, uint16_t *delay_ms, bool *hq_enabled);
esp_err_t dsp_model_set_delay(bool enable, uint16_t delay_ms, bool hq_enabled);
esp_err_t dsp_model_set_delay_path(mvs_path_id_t path, bool enable,
    uint16_t delay_ms, bool hq_enabled);
esp_err_t dsp_model_read_delay_path(mvs_path_id_t path,
    bool *enable, uint16_t *delay_ms, bool *hq_enabled);

// Silence Detector
esp_err_t dsp_model_set_silence_detector(bool enable);

// PreEQ
esp_err_t dsp_model_set_preeq_enable(bool enable);
esp_err_t dsp_model_update_preeq(const mvs_preeq_state_t *state);
esp_err_t dsp_model_update_preeq_path(mvs_path_id_t path,
    const mvs_preeq_state_t *state);
esp_err_t dsp_model_read_preeq_path(mvs_path_id_t path,
    mvs_preeq_state_t *state);

// Out EQ (path-aware)
esp_err_t dsp_model_read_outeq_path(mvs_path_id_t path,
    mvs_preeq_state_t *state);
esp_err_t dsp_model_update_outeq_path(mvs_path_id_t path,
    const mvs_preeq_state_t *state);

// DRC
esp_err_t dsp_model_set_drc_enable(bool enable);
esp_err_t dsp_model_update_drc(const mvs_drc_packed_state_t *state);
esp_err_t dsp_model_update_drc_view_path(mvs_path_id_t path,
    const dsp_drc_view_t *requested, dsp_drc_view_t *confirmed);
esp_err_t dsp_model_set_drc_mode_path(mvs_path_id_t path, uint16_t mode,
    dsp_drc_view_t *confirmed);
esp_err_t dsp_model_read_drc_view_path(mvs_path_id_t path,
    dsp_drc_view_t *view);
esp_err_t dsp_model_profile_drc_view(const dsp_profile_t *profile,
    dsp_drc_view_t *view);

// USB Out Gain
esp_err_t dsp_model_read_usb_out_gain(mvs_path_id_t path, uint16_t *gain_raw);
esp_err_t dsp_model_set_usb_out_gain(mvs_path_id_t path, uint16_t gain_raw);

void dsp_model_profile_apply_drc_view(dsp_profile_t *profile,
    const dsp_drc_view_t *view);
esp_err_t dsp_model_verify_full_profile(const dsp_profile_t *expected);

// Multi-path verify
esp_err_t dsp_model_verify_multi_config(const dsp_multi_config_t *expected);
esp_err_t dsp_model_verify_path_profile(mvs_path_id_t path_id,
    const dsp_profile_t *expected);
const char *dsp_model_get_verify_mismatch(void);

// ---------------------------------------------------------------------------
// Globaler DSP-Status (definiert in main.c)
// ---------------------------------------------------------------------------
extern bool g_dsp_connected;
extern bool g_dsp_ns_state;

#ifdef __cplusplus
}
#endif
