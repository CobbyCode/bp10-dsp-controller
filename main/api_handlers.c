// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// api_handlers.c — REST-API-Endpunkte
//
// Endpunkte:
//   GET  /api/status          – Systemstatus (DSP, WiFi, IP, Lifecycle)
//   GET  /api/dsp             – DSP-Zustand (Readback aller Module)
//   POST /api/dsp/noise       – Noise Suppressor setzen + Readback + Auto-Save
//   POST /api/dsp/bass        – Virtual Bass setzen + Readback + Auto-Save
//   POST /api/dsp/silence     – Silence Detector setzen + Readback + Auto-Save
//   POST /api/dsp/preeq       – PreEQ setzen + Readback + Auto-Save
//   POST /api/dsp/drc         – DRC setzen + Readback + Auto-Save
//   POST /api/dsp/apply       – Vollständige Konfiguration anwenden + Readback + NVS-Save
//   POST /api/dsp/config/export – Aktive DSP-Konfiguration als JSON exportieren
//   POST /api/dsp/config/import – DSP-Konfiguration importieren (validieren, Vorschau)
//   GET  /api/wifi/status     – WiFi-Status (AP + STA getrennt)
//   GET  /api/wifi/config     – Gespeicherte WLAN-Konfiguration (ohne Passwort)
//   POST /api/wifi/config     – Heim-WLAN speichern und verbinden
//   POST /api/wifi/scan       – WLAN-Scan starten
//   GET  /api/wifi/scan       – Scan-Ergebnisse abrufen
//   POST /api/device/name     – Gerätenamen setzen
//   POST /api/device/reset    – Factory Reset (NVS löschen, kein DSP-Flash-Save)
//   POST /api/ota/upload      – Firmware-Binary hochladen
//   GET  /api/ota/status       – OTA-Status (Fortschritt, Fehler, Partition)
//

#include "api_handlers.h"
#include "dsp_model.h"
#include "nvs_settings.h"
#include "wifi_manager.h"
#include "mdns_service.h"
#include "ota_update.h"
#include "config_io.h"
#include "usb_host_ctrl.h"
#include "app_config.h"

#include <string.h>
#include <math.h>
#include <cJSON.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "bp10_api";

static bool profile_uses_a800x_persistence(void)
{
    const mvs_device_profile_t *profile = dsp_model_get_device_profile();
    return profile->valid && profile->kind == MVS_DEVICE_A800X_FIXED;
}

// ---------------------------------------------------------------------------
// Hilfsfunktionen
// ---------------------------------------------------------------------------

static esp_err_t send_json_response(httpd_req_t *req, int status,
                                     const char *json)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_status(req, status == 200 ? "200 OK"
                         : status == 400 ? "400 Bad Request"
                         : status == 404 ? "404 Not Found"
                         : status == 409 ? "409 Conflict"
                         : status == 503 ? "503 Service Unavailable"
                         : "500 Internal Server Error");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_error(httpd_req_t *req, int status, const char *msg)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", msg);
    char *json = cJSON_PrintUnformatted(root);
    esp_err_t ret = send_json_response(req, status, json);
    free(json);
    cJSON_Delete(root);
    return ret;
}

static esp_err_t send_ok(httpd_req_t *req, cJSON *extra)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    if (extra) {
        cJSON_AddItemToObject(root, "data", extra);
    } else {
        cJSON_AddNullToObject(root, "data");
    }
    char *json = cJSON_PrintUnformatted(root);
    esp_err_t ret = send_json_response(req, 200, json);
    free(json);
    cJSON_Delete(root);
    return ret;
}

/**
 * @brief Vollständige DSP-Konfiguration vom Gerät lesen und im NVS speichern.
 *
 * Wird nach jedem erfolgreichen Apply+Readback aufgerufen, damit die
 * Konfiguration beim nächsten Boot/DSP-Reconnect automatisch angewendet wird.
 */
static void auto_save_dsp_config(void)
{
    const mvs_device_profile_t *device = dsp_model_get_device_profile();
    if (!g_dsp_connected || !device->valid ||
        device->kind != MVS_DEVICE_A800X_FIXED) return;

    dsp_profile_t config;
    esp_err_t err = dsp_model_readback(&config);
    if (err == ESP_OK) {
        err = nvs_settings_save_dsp_config(&config);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "DSP-Konfiguration auto-gespeichert");
        }
    } else {
        ESP_LOGW(TAG, "Auto-Save: Readback fehlgeschlagen (%s)", esp_err_to_name(err));
    }
}

// ---------------------------------------------------------------------------
// GET /api/status
// ---------------------------------------------------------------------------

static esp_err_t handler_status_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "running");
    cJSON_AddStringToObject(root, "version", APP_VERSION);

    // DSP
    bool dsp_ok = g_dsp_connected;
    cJSON_AddBoolToObject(root, "dsp_connected", dsp_ok);
    cJSON_AddBoolToObject(root, "dsp_noise_suppressor", dsp_ok ? g_dsp_ns_state : false);
    const mvs_device_profile_t *device_profile = dsp_model_get_device_profile();
    bool a800x = dsp_ok && device_profile->kind == MVS_DEVICE_A800X_FIXED;
    cJSON_AddBoolToObject(root, "dsp_config_saved",
                          a800x && nvs_settings_has_dsp_config());

    cJSON *device = cJSON_AddObjectToObject(root, "device");
    cJSON_AddStringToObject(device, "profile",
        !dsp_ok ? "none" : a800x ? "a800x_fixed" : "generic_acp_classic");
    cJSON_AddNumberToObject(device, "vid", device_profile->vid);
    cJSON_AddNumberToObject(device, "pid", device_profile->pid);
    cJSON_AddNumberToObject(device, "usb_interface", device_profile->usb_interface);
    cJSON_AddBoolToObject(device, "catalog_discovered",
                          device_profile->catalog_discovered);
    cJSON_AddNumberToObject(device, "effect_count", device_profile->catalog_count);

    cJSON *caps = cJSON_AddObjectToObject(root, "capabilities");
    cJSON_AddBoolToObject(caps, "noise_suppressor",
                          dsp_ok && device_profile->noise_suppressor.available);
    cJSON_AddBoolToObject(caps, "virtual_bass",
                          dsp_ok && device_profile->virtual_bass.available);
    cJSON_AddBoolToObject(caps, "silence_detector",
                          dsp_ok && device_profile->silence_detector.available);
    cJSON_AddBoolToObject(caps, "preeq", dsp_ok && device_profile->preeq.available);
    cJSON_AddBoolToObject(caps, "drc", dsp_ok && device_profile->drc.available);
    cJSON_AddStringToObject(caps, "preeq_schema",
        device_profile->preeq_schema == MVS_PEQ_SCHEMA_A800X ? "a800x" :
        device_profile->preeq_schema == MVS_PEQ_SCHEMA_CLASSIC_10BAND ?
        "classic_10band" : "none");
    cJSON_AddStringToObject(caps, "drc_schema",
        device_profile->drc_schema == MVS_DRC_SCHEMA_A800X_4PATH ? "a800x_4path" :
        device_profile->drc_schema == MVS_DRC_SCHEMA_CLASSIC_3BAND ?
        "classic_3band" : "none");
    cJSON_AddNumberToObject(caps, "drc_ratio_step",
        device_profile->drc_schema == MVS_DRC_SCHEMA_CLASSIC_3BAND ? 1.0 : 0.01);

    // MAC
    char mac[18];
    wifi_manager_get_mac_str(mac);
    cJSON_AddStringToObject(root, "mac", mac);

    // Hostname & mDNS
    char hostname[32];
    wifi_manager_get_hostname(hostname, sizeof(hostname));
    cJSON_AddStringToObject(root, "hostname", hostname);
    char mdns_addr[48];
    snprintf(mdns_addr, sizeof(mdns_addr), "%s.local", hostname);
    cJSON_AddStringToObject(root, "mdns_address", mdns_addr);

    // SoftAP
    bool ap_active = wifi_manager_is_softap_active();
    cJSON_AddBoolToObject(root, "ap_active", ap_active);
    if (ap_active) {
        cJSON_AddStringToObject(root, "ap_ssid", hostname);
        cJSON_AddStringToObject(root, "ap_ip", "192.168.4.1");
        cJSON_AddStringToObject(root, "ap_auth", "offen");
    }
    int ap_shutdown_s = wifi_manager_get_ap_shutdown_remaining_sec();
    cJSON_AddNumberToObject(root, "ap_shutdown_remaining_s", ap_shutdown_s);

    // Station
    char ip[16];
    bool sta_connected = wifi_manager_is_connected();
    cJSON_AddBoolToObject(root, "sta_connected", sta_connected);
    if (sta_connected && wifi_manager_get_ip_str(ip, sizeof(ip)) == ESP_OK) {
        cJSON_AddStringToObject(root, "sta_ip", ip);
    }
    char sta_ssid[33];
    if (wifi_manager_get_sta_ssid(sta_ssid, sizeof(sta_ssid)) == ESP_OK) {
        cJSON_AddStringToObject(root, "sta_ssid", sta_ssid);
    }
    cJSON_AddStringToObject(root, "lifecycle_state",
                            wifi_manager_lifecycle_state_str());

    char *json = cJSON_PrintUnformatted(root);
    esp_err_t ret = send_json_response(req, 200, json);
    free(json);
    cJSON_Delete(root);
    return ret;
}

// ---------------------------------------------------------------------------
// GET /api/dsp – kompletter DSP-Zustand (Readback aller Module)
// ---------------------------------------------------------------------------

static esp_err_t handler_dsp_get(httpd_req_t *req)
{
    if (!g_dsp_connected) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "dsp", "unavailable");
        cJSON_AddStringToObject(root, "error", "Kein DSP angeschlossen");
        char *json = cJSON_PrintUnformatted(root);
        esp_err_t ret = send_json_response(req, 200, json);
        free(json);
        cJSON_Delete(root);
        return ret;
    }

    dsp_profile_t profile;
    esp_err_t err = dsp_model_readback(&profile);
    if (err != ESP_OK) {
        return send_error(req, 500, "DSP readback failed");
    }

    cJSON *root = cJSON_CreateObject();
    int64_t readback_ms = esp_timer_get_time() / 1000;
    cJSON_AddBoolToObject(root, "noise_suppressor",
                          profile.noise_suppressor_enabled);
    cJSON_AddNumberToObject(root, "noise_threshold_db",
                            profile.noise_suppressor_threshold_raw / 100.0);
    cJSON_AddNumberToObject(root, "noise_ratio",
                            profile.noise_suppressor_ratio);
    cJSON_AddNumberToObject(root, "noise_attack_ms",
                            profile.noise_suppressor_attack_ms);
    cJSON_AddNumberToObject(root, "noise_release_ms",
                            profile.noise_suppressor_release_ms);
    cJSON_AddBoolToObject(root, "virtual_bass",
                          profile.virtual_bass_enabled);
    cJSON_AddNumberToObject(root, "bass_cutoff_hz", profile.virtual_bass_cutoff_hz);
    cJSON_AddNumberToObject(root, "bass_intensity_pct", profile.virtual_bass_intensity_pct);
    cJSON_AddBoolToObject(root, "bass_enhanced", profile.virtual_bass_enhanced);
    bool silence_enabled = false;
    uint16_t silence_amplitude = 0;
    esp_err_t silence_err = dsp_model_read_silence_detector(&silence_enabled,
                                                             &silence_amplitude);
    cJSON *silence = cJSON_AddObjectToObject(root, "silence");
    cJSON_AddBoolToObject(silence, "valid", silence_err == ESP_OK);
    cJSON_AddBoolToObject(silence, "read_success", silence_err == ESP_OK);
    cJSON_AddNumberToObject(silence, "readback_ms", (double)readback_ms);
    if (silence_err == ESP_OK) {
        cJSON_AddBoolToObject(silence, "enabled", silence_enabled);
        cJSON_AddNumberToObject(silence, "amplitude", silence_amplitude);
        cJSON_AddBoolToObject(root, "silence_detector", silence_enabled);
    }

    mvs_preeq_state_t preeq_state;
    esp_err_t preeq_err = dsp_model_read_preeq(&preeq_state);
    cJSON *preeq = cJSON_AddObjectToObject(root, "preeq");
    cJSON_AddBoolToObject(preeq, "valid", preeq_err == ESP_OK);
    cJSON_AddBoolToObject(preeq, "read_success", preeq_err == ESP_OK);
    cJSON_AddNumberToObject(preeq, "readback_ms", (double)readback_ms);
    cJSON *filters = cJSON_AddArrayToObject(preeq, "filters");
    if (preeq_err == ESP_OK) {
        cJSON_AddBoolToObject(preeq, "enabled", preeq_state.block_enabled != 0);
        cJSON_AddNumberToObject(preeq, "pregain_db", preeq_state.pre_gain_raw / 256.0);
        cJSON_AddBoolToObject(root, "preeq_enabled", preeq_state.block_enabled != 0);
        cJSON_AddNumberToObject(root, "preeq_pregain_db", preeq_state.pre_gain_raw / 256.0);
        cJSON *legacy_filters = cJSON_AddArrayToObject(root, "preeq_filters");
        for (int i = 0; i < 10; i++) {
            const mvs_preeq_filter_t *filter = &preeq_state.filters[i];
            cJSON *item = cJSON_CreateObject();
            cJSON_AddBoolToObject(item, "enabled", filter->enabled != 0);
            cJSON_AddNumberToObject(item, "type", filter->type);
            cJSON_AddNumberToObject(item, "frequency_hz", filter->frequency_hz);
            cJSON_AddNumberToObject(item, "q", filter->q_raw / 1024.0);
            cJSON_AddNumberToObject(item, "gain_db", filter->gain_raw / 256.0);
            cJSON_AddItemToArray(filters, cJSON_Duplicate(item, true));
            cJSON_AddItemToArray(legacy_filters, item);
        }
    }
    dsp_drc_view_t drc_view;
    esp_err_t drc_err = dsp_model_read_drc_view(&drc_view);
    cJSON *drc = cJSON_AddObjectToObject(root, "drc");
    cJSON_AddBoolToObject(drc, "valid", drc_err == ESP_OK);
    cJSON_AddBoolToObject(drc, "read_success", drc_err == ESP_OK);
    cJSON_AddNumberToObject(drc, "readback_ms", (double)readback_ms);
    if (drc_err == ESP_OK) {
        cJSON_AddBoolToObject(drc, "enabled", drc_view.enabled);
        cJSON_AddBoolToObject(drc, "full_band_supported", drc_view.full_band_supported);
        cJSON_AddNumberToObject(drc, "pregain_db", drc_view.pregain_db);
        cJSON_AddNumberToObject(drc, "threshold_db", drc_view.threshold_db);
        cJSON_AddNumberToObject(drc, "ratio", drc_view.ratio);
        cJSON_AddNumberToObject(drc, "attack_ms", drc_view.attack_ms);
        cJSON_AddNumberToObject(drc, "release_ms", drc_view.release_ms);
        cJSON_AddBoolToObject(root, "drc_enabled", drc_view.enabled);
    }

    const mvs_device_profile_t *device = dsp_model_get_device_profile();
    cJSON_AddBoolToObject(root, "dsp_config_saved",
        device->kind == MVS_DEVICE_A800X_FIXED && nvs_settings_has_dsp_config());

    char *json = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "DSP JSON: %s", json);
    esp_err_t ret = send_json_response(req, 200, json);
    free(json);
    cJSON_Delete(root);
    return ret;
}

// ---------------------------------------------------------------------------
// POST /api/dsp/noise – Noise Suppressor + Auto-Save
// ---------------------------------------------------------------------------

static esp_err_t handler_dsp_noise_post(httpd_req_t *req)
{
    if (!g_dsp_connected) {
        return send_error(req, 503, "DSP unavailable");
    }

    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return send_error(req, 400, "No data");
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) return send_error(req, 400, "Invalid JSON");

    cJSON *enable = cJSON_GetObjectItemCaseSensitive(json, "enable");
    cJSON *threshold = cJSON_GetObjectItemCaseSensitive(json, "threshold_db");
    cJSON *ratio = cJSON_GetObjectItemCaseSensitive(json, "ratio");
    cJSON *attack = cJSON_GetObjectItemCaseSensitive(json, "attack_ms");
    cJSON *release = cJSON_GetObjectItemCaseSensitive(json, "release_ms");
    if (!cJSON_IsBool(enable)) {
        cJSON_Delete(json);
        return send_error(req, 400, "Missing 'enable' field");
    }

    bool requested = cJSON_IsTrue(enable);
    esp_err_t err;
    bool full_state = cJSON_IsNumber(threshold) || cJSON_IsNumber(ratio) ||
                      cJSON_IsNumber(attack) || cJSON_IsNumber(release);
    int16_t threshold_raw = 0;
    uint16_t requested_ratio = 0, requested_attack = 0, requested_release = 0;

    if (full_state) {
        if (!cJSON_IsNumber(threshold) || !cJSON_IsNumber(ratio) ||
            !cJSON_IsNumber(attack) || !cJSON_IsNumber(release) ||
            threshold->valuedouble < -327.68 || threshold->valuedouble > 0.0 ||
            ratio->valuedouble < 0 || ratio->valuedouble > UINT16_MAX ||
            attack->valuedouble < 0 || attack->valuedouble > UINT16_MAX ||
            release->valuedouble < 0 || release->valuedouble > UINT16_MAX) {
            cJSON_Delete(json);
            return send_error(req, 400, "Invalid Noise Suppressor values");
        }
        threshold_raw = (int16_t)(threshold->valuedouble * 100.0);
        requested_ratio = (uint16_t)ratio->valuedouble;
        requested_attack = (uint16_t)attack->valuedouble;
        requested_release = (uint16_t)release->valuedouble;
        err = dsp_model_set_noise_suppressor_state(requested, threshold_raw,
                                                   requested_ratio,
                                                   requested_attack,
                                                   requested_release);
    } else {
        err = dsp_model_set_noise_suppressor(requested);
    }
    cJSON_Delete(json);

    if (err != ESP_OK) {
        return send_error(req, 500, "Failed to set Noise Suppressor");
    }
    dsp_profile_t readback;
    err = dsp_model_readback(&readback);
    bool confirmed = err == ESP_OK &&
                     readback.noise_suppressor_enabled == requested;
    // OFF intentionally writes only the block-enable selector. Parameter
    // writes are rejected by this DSP while the block is disabled.
    if (confirmed && full_state && requested) {
        confirmed = readback.noise_suppressor_threshold_raw == threshold_raw &&
                    readback.noise_suppressor_ratio == requested_ratio &&
                    readback.noise_suppressor_attack_ms == requested_attack &&
                    readback.noise_suppressor_release_ms == requested_release;
    }
    if (!confirmed) {
        return send_error(req, 500, "Noise Suppressor readback mismatch");
    }
    g_dsp_ns_state = readback.noise_suppressor_enabled;

    // Auto-Save: vollständige Konfiguration im NVS persistieren
    auto_save_dsp_config();

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "enabled", readback.noise_suppressor_enabled);
    cJSON_AddNumberToObject(result, "threshold_db",
                            readback.noise_suppressor_threshold_raw / 100.0);
    cJSON_AddNumberToObject(result, "ratio", readback.noise_suppressor_ratio);
    cJSON_AddNumberToObject(result, "attack_ms", readback.noise_suppressor_attack_ms);
    cJSON_AddNumberToObject(result, "release_ms", readback.noise_suppressor_release_ms);
    cJSON_AddBoolToObject(result, "confirmed", true);
    cJSON_AddBoolToObject(result, "saved", profile_uses_a800x_persistence());
    return send_ok(req, result);
}

// ---------------------------------------------------------------------------
// POST /api/dsp/bass – Virtual Bass + Auto-Save
// ---------------------------------------------------------------------------

static esp_err_t handler_dsp_bass_post(httpd_req_t *req)
{
    if (!g_dsp_connected) {
        return send_error(req, 503, "DSP unavailable");
    }

    char buf[192];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return send_error(req, 400, "No data");
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) return send_error(req, 400, "Invalid JSON");

    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    cJSON *cutoff = cJSON_GetObjectItem(json, "cutoff_hz");
    cJSON *intensity = cJSON_GetObjectItem(json, "intensity_pct");
    cJSON *enhanced = cJSON_GetObjectItem(json, "bass_enhanced");
    if (!cJSON_IsBool(enable)) {
        cJSON_Delete(json);
        return send_error(req, 400, "Missing 'enable' field");
    }

    bool requested = cJSON_IsTrue(enable);
    bool full_state = cJSON_IsNumber(cutoff) || cJSON_IsNumber(intensity) || cJSON_IsBool(enhanced);
    uint16_t requested_cutoff = 0, requested_intensity = 0;
    bool requested_enhanced = false;
    esp_err_t err;
    if (full_state) {
        if (!cJSON_IsNumber(cutoff) || !cJSON_IsNumber(intensity) || !cJSON_IsBool(enhanced) ||
            cutoff->valuedouble < 0 || cutoff->valuedouble > UINT16_MAX ||
            intensity->valuedouble < 0 || intensity->valuedouble > UINT16_MAX) {
            cJSON_Delete(json);
            return send_error(req, 400, "Invalid Virtual Bass values");
        }
        requested_cutoff = (uint16_t)cutoff->valuedouble;
        requested_intensity = (uint16_t)intensity->valuedouble;
        requested_enhanced = cJSON_IsTrue(enhanced);
        err = dsp_model_set_virtual_bass_state(requested, requested_cutoff,
                                               requested_intensity, requested_enhanced);
    } else {
        err = dsp_model_set_virtual_bass(requested);
    }
    cJSON_Delete(json);

    if (err != ESP_OK) {
        return send_error(req, 500, "Failed to set Virtual Bass");
    }

    dsp_profile_t readback;
    err = dsp_model_readback(&readback);
    bool confirmed = err == ESP_OK && readback.virtual_bass_enabled == requested;
    // An OFF operation intentionally writes only the block-enable selector.
    // The remaining parameters stay unchanged and must not be compared with
    // possibly edited/stale form values. ON writes and verifies all fields.
    if (confirmed && full_state && requested) {
        confirmed = readback.virtual_bass_cutoff_hz == requested_cutoff &&
                    readback.virtual_bass_intensity_pct == requested_intensity &&
                    readback.virtual_bass_enhanced == requested_enhanced;
    }
    if (!confirmed) {
        return send_error(req, 500, "Virtual Bass readback mismatch");
    }

    auto_save_dsp_config();

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "enabled", readback.virtual_bass_enabled);
    cJSON_AddNumberToObject(result, "cutoff_hz", readback.virtual_bass_cutoff_hz);
    cJSON_AddNumberToObject(result, "intensity_pct", readback.virtual_bass_intensity_pct);
    cJSON_AddBoolToObject(result, "bass_enhanced", readback.virtual_bass_enhanced);
    cJSON_AddBoolToObject(result, "confirmed", true);
    cJSON_AddBoolToObject(result, "saved", profile_uses_a800x_persistence());
    return send_ok(req, result);
}

// ---------------------------------------------------------------------------
// Toggle-Handler (Silence, PreEQ-Enable, DRC-Enable) + Auto-Save
// ---------------------------------------------------------------------------

typedef esp_err_t (*dsp_toggle_fn_t)(bool enable);

static esp_err_t handler_dsp_toggle_confirmed(httpd_req_t *req,
                                               dsp_toggle_fn_t setter,
                                               uint8_t effect_id,
                                               const char *name)
{
    if (!g_dsp_connected) {
        return send_error(req, 503, "DSP unavailable");
    }

    char buf[64];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return send_error(req, 400, "No data");
    buf[len] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) return send_error(req, 400, "Invalid JSON");
    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    if (!cJSON_IsBool(enable)) {
        cJSON_Delete(json);
        return send_error(req, 400, "Missing 'enable' field");
    }
    bool requested = cJSON_IsTrue(enable);
    cJSON_Delete(json);

    esp_err_t err = setter(requested);
    if (err != ESP_OK) return send_error(req, 500, name);

    bool confirmed = false;
    err = dsp_model_read_effect_enabled(effect_id, &confirmed);
    if (err != ESP_OK || confirmed != requested) {
        return send_error(req, 500, "DSP readback mismatch");
    }

    auto_save_dsp_config();

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "enabled", confirmed);
    cJSON_AddBoolToObject(result, "confirmed", true);
    cJSON_AddBoolToObject(result, "saved", profile_uses_a800x_persistence());
    return send_ok(req, result);
}

static esp_err_t handler_dsp_silence_post(httpd_req_t *req)
{
    return handler_dsp_toggle_confirmed(req, dsp_model_set_silence_detector,
                                        dsp_model_get_effect_id_sd(),
                                        "Failed to set Silence Detector");
}

// ---------------------------------------------------------------------------
// POST /api/dsp/preeq – PreEQ + Auto-Save
// ---------------------------------------------------------------------------

static esp_err_t handler_dsp_preeq_post(httpd_req_t *req)
{
    if (!g_dsp_connected) {
        return send_error(req, 503, "DSP unavailable");
    }
    if (req->content_len <= 0 || req->content_len > 4096) {
        return send_error(req, 400, "Invalid PreEQ data length");
    }

    char *buf = malloc(req->content_len + 1);
    if (!buf) return send_error(req, 500, "Out of memory");
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, buf + received, req->content_len - received);
        if (n <= 0) { free(buf); return send_error(req, 400, "Incomplete PreEQ data"); }
        received += n;
    }
    buf[received] = '\0';
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (!json) return send_error(req, 400, "Invalid JSON");

    dsp_profile_t current;
    esp_err_t err = dsp_model_readback(&current);
    if (err != ESP_OK) { cJSON_Delete(json); return send_error(req, 500, "PreEQ readback failed"); }
    mvs_preeq_state_t requested = current.preeq;

    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    cJSON *pregain = cJSON_GetObjectItem(json, "pregain_db");
    cJSON *changes = cJSON_GetObjectItem(json, "filters");
    if (!cJSON_IsBool(enable) || !cJSON_IsNumber(pregain) || !cJSON_IsArray(changes) ||
        pregain->valuedouble < -128.0 || pregain->valuedouble > 127.996) {
        cJSON_Delete(json);
        return send_error(req, 400, "Invalid PreEQ base values");
    }
    requested.block_enabled = cJSON_IsTrue(enable) ? 1 : 0;
    double pregain_scaled = pregain->valuedouble * 256.0;
    requested.pre_gain_raw = (int16_t)(pregain_scaled + (pregain_scaled >= 0 ? 0.5 : -0.5));
    const mvs_device_profile_t *device_profile = dsp_model_get_device_profile();
    int max_filter_type = device_profile->preeq_schema ==
        MVS_PEQ_SCHEMA_CLASSIC_10BAND ? MVS_FILTER_NH : MVS_FILTER_HO;

    cJSON *change;
    cJSON_ArrayForEach(change, changes) {
        cJSON *index = cJSON_GetObjectItem(change, "index");
        if (!cJSON_IsNumber(index) || index->valueint < 0 || index->valueint > 9) {
            cJSON_Delete(json); return send_error(req, 400, "Invalid PreEQ filter index");
        }
        mvs_preeq_filter_t *filter = &requested.filters[index->valueint];
        cJSON *value;
        value = cJSON_GetObjectItem(change, "enabled");
        if (value) { if (!cJSON_IsBool(value)) { cJSON_Delete(json); return send_error(req, 400, "Invalid filter enabled value"); } filter->enabled = cJSON_IsTrue(value) ? 1 : 0; }
        value = cJSON_GetObjectItem(change, "type");
        if (value) { if (!cJSON_IsNumber(value) || value->valueint < 0 || value->valueint > max_filter_type) { cJSON_Delete(json); return send_error(req, 400, "Invalid filter type for this device profile"); } filter->type = value->valueint; }
        value = cJSON_GetObjectItem(change, "frequency_hz");
        if (value) { if (!cJSON_IsNumber(value) || value->valuedouble < 1 || value->valuedouble > UINT16_MAX) { cJSON_Delete(json); return send_error(req, 400, "Invalid filter frequency"); } filter->frequency_hz = (uint16_t)value->valuedouble; }
        value = cJSON_GetObjectItem(change, "q");
        if (value) { if (!cJSON_IsNumber(value) || value->valuedouble <= 0 || value->valuedouble > 63.999) { cJSON_Delete(json); return send_error(req, 400, "Invalid Q value"); } filter->q_raw = (uint16_t)(value->valuedouble * 1024.0 + 0.5); }
        value = cJSON_GetObjectItem(change, "gain_db");
        if (value) { if (!cJSON_IsNumber(value) || value->valuedouble < -128.0 || value->valuedouble > 127.996) { cJSON_Delete(json); return send_error(req, 400, "Invalid filter gain"); } double scaled = value->valuedouble * 256.0; filter->gain_raw = (int16_t)(scaled + (scaled >= 0 ? 0.5 : -0.5)); }
    }
    cJSON_Delete(json);

    err = dsp_model_update_preeq(&requested);
    if (err != ESP_OK) return send_error(req, 500, "Failed to write PreEQ");

    // Gleiche Normalisierung wie in dsp_model_update_preeq anwenden,
    // damit der memcmp-Vergleich nicht an reparierten korrumpierten
    // deaktivierten Filtern scheitert.
    for (int i = 0; i < 10; i++) {
        mvs_preeq_filter_t *f = &requested.filters[i];
        if (!f->enabled && f->frequency_hz == 0 && f->q_raw == 0) {
            f->type = MVS_FILTER_PK;
            f->frequency_hz = 20000;
            f->q_raw = 724;
            f->gain_raw = 0;
        }
    }
    mvs_prepare_preeq_for_schema(device_profile->preeq_schema, &requested);

    dsp_profile_t readback;
    err = dsp_model_readback(&readback);
    if (err != ESP_OK || memcmp(&requested, &readback.preeq, sizeof(requested)) != 0) {
        return send_error(req, 500, "PreEQ readback mismatch");
    }

    auto_save_dsp_config();

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "enabled", readback.preeq.block_enabled != 0);
    cJSON_AddNumberToObject(result, "pregain_db", readback.preeq.pre_gain_raw / 256.0);
    cJSON_AddBoolToObject(result, "confirmed", true);
    cJSON_AddBoolToObject(result, "saved", profile_uses_a800x_persistence());
    return send_ok(req, result);
}

// ---------------------------------------------------------------------------
// POST /api/dsp/drc – DRC + Auto-Save
// ---------------------------------------------------------------------------

static esp_err_t handler_dsp_drc_post(httpd_req_t *req)
{
    if (!g_dsp_connected) {
        return send_error(req, 503, "DSP unavailable");
    }
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return send_error(req, 400, "No data");
    buf[len] = '\0';
    cJSON *json = cJSON_Parse(buf);
    if (!json) return send_error(req, 400, "Invalid JSON");

    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    cJSON *pregain = cJSON_GetObjectItem(json, "pregain_db");
    cJSON *threshold = cJSON_GetObjectItem(json, "threshold_db");
    cJSON *ratio = cJSON_GetObjectItem(json, "ratio");
    cJSON *attack = cJSON_GetObjectItem(json, "attack_ms");
    cJSON *release = cJSON_GetObjectItem(json, "release_ms");
    if (!cJSON_IsBool(enable) || !cJSON_IsNumber(pregain) ||
        !cJSON_IsNumber(threshold) || !cJSON_IsNumber(ratio) ||
        !cJSON_IsNumber(attack) || !cJSON_IsNumber(release) ||
        pregain->valuedouble < -72.0 || pregain->valuedouble > 24.0 ||
        threshold->valuedouble < -327.68 || threshold->valuedouble > 0.0 ||
        ratio->valuedouble < 0.01 || ratio->valuedouble > 655.35 ||
        attack->valuedouble < 0 || attack->valuedouble > UINT16_MAX ||
        release->valuedouble < 0 || release->valuedouble > UINT16_MAX) {
        cJSON_Delete(json);
        return send_error(req, 400, "Invalid Full-Band DRC values");
    }

    const mvs_device_profile_t *device = dsp_model_get_device_profile();
    if (device->drc_schema == MVS_DRC_SCHEMA_CLASSIC_3BAND &&
        fabs(ratio->valuedouble - round(ratio->valuedouble)) > 0.000001) {
        cJSON_Delete(json);
        return send_error(req, 400, "Classic DRC ratio must be an integer");
    }
    dsp_drc_view_t requested = {
        .valid = true,
        .enabled = cJSON_IsTrue(enable),
        .full_band_supported = true,
        .pregain_db = pregain->valuedouble,
        .threshold_db = threshold->valuedouble,
        .ratio = ratio->valuedouble,
        .attack_ms = (uint16_t)lround(attack->valuedouble),
        .release_ms = (uint16_t)lround(release->valuedouble),
    };
    cJSON_Delete(json);

    dsp_drc_view_t confirmed;
    esp_err_t err = dsp_model_update_drc_view(&requested, &confirmed);
    if (err == ESP_ERR_INVALID_STATE)
        return send_error(req, 409,
                          "DRC is not in supported Full-Band mode; no values were changed");
    if (err != ESP_OK) return send_error(req, 500, "DRC write/readback failed");

    auto_save_dsp_config();

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "enabled", confirmed.enabled);
    cJSON_AddBoolToObject(result, "full_band_supported",
                          confirmed.full_band_supported);
    cJSON_AddNumberToObject(result, "pregain_db", confirmed.pregain_db);
    cJSON_AddNumberToObject(result, "threshold_db", confirmed.threshold_db);
    cJSON_AddNumberToObject(result, "ratio", confirmed.ratio);
    cJSON_AddNumberToObject(result, "attack_ms", confirmed.attack_ms);
    cJSON_AddNumberToObject(result, "release_ms", confirmed.release_ms);
    cJSON_AddBoolToObject(result, "confirmed", true);
    cJSON_AddBoolToObject(result, "saved",
                          device->kind == MVS_DEVICE_A800X_FIXED);
    return send_ok(req, result);
}

// ---------------------------------------------------------------------------
// POST /api/dsp/apply – Vollständige Konfiguration anwenden + Readback + NVS
// ---------------------------------------------------------------------------

static esp_err_t handler_dsp_apply_post(httpd_req_t *req)
{
    if (!g_dsp_connected) {
        return send_error(req, 503, "DSP unavailable");
    }
    if (!profile_uses_a800x_persistence())
        return send_error(req, 409, "Not supported for this device profile");

    char *buf = NULL;
    if (req->content_len > 0 && req->content_len <= 8192) {
        buf = malloc(req->content_len + 1);
        if (!buf) return send_error(req, 500, "Out of memory");
        size_t received = 0;
        while (received < req->content_len) {
            int n = httpd_req_recv(req, buf + received, req->content_len - received);
            if (n <= 0) { free(buf); return send_error(req, 400, "Incomplete data"); }
            received += n;
        }
        buf[received] = '\0';
    }

    dsp_profile_t profile;
    bool from_body = false;

    if (buf && buf[0] == '{') {
        // JSON-Body: parse via config_io
        esp_err_t err = config_io_parse_import(buf, &profile);
        free(buf);
        if (err != ESP_OK) {
            return send_error(req, 400, "Invalid import JSON in request body");
        }
        from_body = true;
    } else {
        // Kein Body oder kein JSON: NVS-Konfiguration verwenden
        free(buf);
        if (!nvs_settings_has_dsp_config()) {
            return send_error(req, 400, "No saved DSP configuration to apply");
        }
        esp_err_t err = nvs_settings_load_dsp_config(&profile);
        if (err != ESP_OK) {
            return send_error(req, 500, "Failed to load saved DSP configuration");
        }
    }

    // Auf DSP anwenden
    esp_err_t err = dsp_model_apply_profile(&profile);
    if (err != ESP_OK) {
        return send_error(req, 500, "Failed to apply DSP configuration");
    }

    // Readback aller Module bestätigen
    dsp_profile_t readback;
    err = dsp_model_readback(&readback);
    if (err != ESP_OK) {
        return send_error(req, 500, "DSP readback failed after apply");
    }

    // Im NVS speichern
    nvs_settings_save_dsp_config(&readback);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "applied", true);
    cJSON_AddBoolToObject(result, "confirmed", true);
    cJSON_AddBoolToObject(result, "saved", true);
    cJSON_AddStringToObject(result, "source", from_body ? "body" : "nvs");
    return send_ok(req, result);
}

// ---------------------------------------------------------------------------
// POST /api/dsp/config/export – DSP-Konfiguration als JSON exportieren
// ---------------------------------------------------------------------------

static esp_err_t handler_config_export_post(httpd_req_t *req)
{
    if (!profile_uses_a800x_persistence())
        return send_error(req, 409, "Not supported for this device profile");
    char *json = NULL;
    esp_err_t err = config_io_export(&json);
    if (err != ESP_OK || !json) {
        return send_error(req, 500, "Export failed");
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    esp_err_t ret = httpd_resp_sendstr(req, json);
    free(json);
    return ret;
}

// ---------------------------------------------------------------------------
// POST /api/dsp/config/import – JSON validieren & Vorschau zurückgeben
// ---------------------------------------------------------------------------

static esp_err_t handler_config_import_post(httpd_req_t *req)
{
    if (!profile_uses_a800x_persistence())
        return send_error(req, 409, "Not supported for this device profile");
    char *buf = NULL;
    if (req->content_len <= 0 || req->content_len > 32768) {
        return send_error(req, 400, "Invalid content length");
    }
    buf = malloc(req->content_len + 1);
    if (!buf) return send_error(req, 500, "Out of memory");
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, buf + received, req->content_len - received);
        if (n <= 0) { free(buf); return send_error(req, 400, "Incomplete import data"); }
        received += n;
    }
    buf[received] = '\0';

    dsp_profile_t profile;
    esp_err_t err = config_io_parse_import(buf, &profile);
    free(buf);
    if (err != ESP_OK) {
        return send_error(req, 400, "Invalid DSP configuration JSON");
    }

    // Geparste Konfiguration als Vorschau zurückgeben (nicht anwenden!)
    cJSON *preview = cJSON_CreateObject();
    cJSON_AddBoolToObject(preview, "valid", true);
    cJSON_AddStringToObject(preview, "message",
        "Configuration validated. Click Apply to send to DSP.");

    cJSON *dsp = cJSON_AddObjectToObject(preview, "dsp");
    cJSON *ns = cJSON_AddObjectToObject(dsp, "noise_suppressor");
    cJSON_AddBoolToObject(ns, "enabled", profile.noise_suppressor_enabled);
    cJSON_AddNumberToObject(ns, "threshold_db", profile.noise_suppressor_threshold_raw / 100.0);
    cJSON_AddNumberToObject(ns, "ratio", profile.noise_suppressor_ratio);
    cJSON_AddNumberToObject(ns, "attack_ms", profile.noise_suppressor_attack_ms);
    cJSON_AddNumberToObject(ns, "release_ms", profile.noise_suppressor_release_ms);

    cJSON *vb = cJSON_AddObjectToObject(dsp, "virtual_bass");
    cJSON_AddBoolToObject(vb, "enabled", profile.virtual_bass_enabled);
    cJSON_AddNumberToObject(vb, "cutoff_hz", profile.virtual_bass_cutoff_hz);
    cJSON_AddNumberToObject(vb, "intensity_pct", profile.virtual_bass_intensity_pct);
    cJSON_AddBoolToObject(vb, "bass_enhanced", profile.virtual_bass_enhanced);

    cJSON *sd = cJSON_AddObjectToObject(dsp, "silence_detector");
    cJSON_AddBoolToObject(sd, "enabled", profile.silence_detector_enabled);

    cJSON *preeq = cJSON_AddObjectToObject(dsp, "preeq");
    cJSON_AddBoolToObject(preeq, "enabled", profile.preeq.block_enabled != 0);
    cJSON_AddNumberToObject(preeq, "pregain_db", profile.preeq.pre_gain_raw / 256.0);

    cJSON *drc = cJSON_AddObjectToObject(dsp, "drc");
    cJSON_AddBoolToObject(drc, "enabled", profile.drc.enabled != 0);
    cJSON_AddNumberToObject(drc, "mode", profile.drc.mode);

    return send_ok(req, preview);
}

// ---------------------------------------------------------------------------
// WiFi-Handler (unverändert)
// ---------------------------------------------------------------------------

static esp_err_t handler_wifi_status_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    char hostname[32];
    wifi_manager_get_hostname(hostname, sizeof(hostname));
    cJSON_AddStringToObject(root, "hostname", hostname);
    char mdns_addr[48];
    snprintf(mdns_addr, sizeof(mdns_addr), "%s.local", hostname);
    cJSON_AddStringToObject(root, "mdns_address", mdns_addr);

    bool ap_active = wifi_manager_is_softap_active();
    cJSON_AddBoolToObject(root, "ap_active", ap_active);
    if (ap_active) {
        cJSON_AddStringToObject(root, "ap_ssid", hostname);
        cJSON_AddStringToObject(root, "ap_ip", "192.168.4.1");
        cJSON_AddStringToObject(root, "ap_auth", "offen");
    }
    int ap_shutdown_s = wifi_manager_get_ap_shutdown_remaining_sec();
    cJSON_AddNumberToObject(root, "ap_shutdown_remaining_s", ap_shutdown_s);

    char ip[16];
    bool sta_connected = wifi_manager_is_connected();
    cJSON_AddBoolToObject(root, "sta_connected", sta_connected);
    if (sta_connected && wifi_manager_get_ip_str(ip, sizeof(ip)) == ESP_OK) {
        cJSON_AddStringToObject(root, "sta_ip", ip);
    }
    char sta_ssid[33];
    if (wifi_manager_get_sta_ssid(sta_ssid, sizeof(sta_ssid)) == ESP_OK) {
        cJSON_AddStringToObject(root, "sta_ssid", sta_ssid);
    }
    char connection_state[16];
    char connection_message[80];
    wifi_manager_get_connection_status(connection_state, sizeof(connection_state),
                                       connection_message, sizeof(connection_message));
    cJSON_AddStringToObject(root, "connection_state", connection_state);
    cJSON_AddStringToObject(root, "connection_message", connection_message);
    cJSON_AddStringToObject(root, "lifecycle_state",
                            wifi_manager_lifecycle_state_str());

    char *json = cJSON_PrintUnformatted(root);
    esp_err_t err = send_json_response(req, 200, json);
    free(json);
    cJSON_Delete(root);
    return err;
}

static esp_err_t handler_wifi_scan_post(httpd_req_t *req)
{
    wifi_manager_note_user_activity();
    esp_err_t err = wifi_manager_start_scan();
    if (err == ESP_ERR_INVALID_STATE) return send_error(req, 400, "A Wi-Fi scan is already running");
    if (err != ESP_OK) return send_error(req, 500, "Unable to start Wi-Fi scan");
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "state", "scanning");
    return send_ok(req, result);
}

static const char *wifi_quality(int rssi)
{
    if (rssi >= -55) return "Excellent";
    if (rssi >= -67) return "Good";
    if (rssi >= -75) return "Fair";
    return "Weak";
}

static esp_err_t handler_wifi_scan_get(httpd_req_t *req)
{
    wifi_manager_scan_result_t results[WIFI_MANAGER_SCAN_MAX_RESULTS];
    size_t count = 0;
    esp_err_t scan_error = ESP_OK;
    wifi_manager_scan_state_t state = wifi_manager_get_scan_results(
        results, WIFI_MANAGER_SCAN_MAX_RESULTS, &count, &scan_error);
    cJSON *root = cJSON_CreateObject();
    const char *state_name = state == WIFI_MANAGER_SCAN_RUNNING ? "scanning" :
                             state == WIFI_MANAGER_SCAN_DONE ? "done" :
                             state == WIFI_MANAGER_SCAN_FAILED ? "failed" : "idle";
    cJSON_AddStringToObject(root, "state", state_name);
    if (state == WIFI_MANAGER_SCAN_FAILED) {
        cJSON_AddStringToObject(root, "message", "Wi-Fi scan failed; please try again");
    }
    cJSON *networks = cJSON_AddArrayToObject(root, "networks");
    for (size_t i = 0; i < count; ++i) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", results[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", results[i].rssi);
        cJSON_AddStringToObject(item, "quality", wifi_quality(results[i].rssi));
        cJSON_AddBoolToObject(item, "secure", results[i].secure);
        cJSON_AddItemToArray(networks, item);
    }
    char *json = cJSON_PrintUnformatted(root);
    esp_err_t result = send_json_response(req, 200, json);
    free(json);
    cJSON_Delete(root);
    (void)scan_error;
    return result;
}

static esp_err_t handler_wifi_config_get(httpd_req_t *req)
{
    wifi_creds_t creds = {0};
    bool configured = nvs_settings_load_wifi_creds(&creds) == ESP_OK &&
                      creds.ssid[0] != '\0';
    device_config_t config = {
        .wifi_auto_off = false,
        .wifi_setup_timeout_s = BP10_WIFI_SETUP_TIMEOUT_S,
    };
    if (nvs_settings_load_config(&config) != ESP_OK) {
        config.wifi_auto_off = false;
        config.wifi_setup_timeout_s = BP10_WIFI_SETUP_TIMEOUT_S;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "configured", configured);
    cJSON_AddStringToObject(root, "ssid", configured ? creds.ssid : "");
    cJSON_AddBoolToObject(root, "password_saved", configured && creds.password[0] != '\0');
    cJSON_AddBoolToObject(root, "auto_off", config.wifi_auto_off);
    cJSON_AddNumberToObject(root, "initial_timeout_s", config.wifi_setup_timeout_s);
    cJSON_AddNumberToObject(root, "user_idle_timeout_s", 1800);

    char *json = cJSON_PrintUnformatted(root);
    esp_err_t result = send_json_response(req, 200, json);
    free(json);
    cJSON_Delete(root);
    return result;
}

static esp_err_t handler_wifi_config_post(httpd_req_t *req)
{
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return send_error(req, 400, "No data");
    buf[len] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) return send_error(req, 400, "Invalid JSON");
    cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    cJSON *password = cJSON_GetObjectItem(json, "password");
    cJSON *auto_off = cJSON_GetObjectItem(json, "auto_off");
    if (!cJSON_IsString(ssid) || ssid->valuestring[0] == '\0' ||
        strlen(ssid->valuestring) >= BP10_WIFI_SSID_MAX_LEN) {
        cJSON_Delete(json);
        return send_error(req, 400, "SSID must be 1 to 31 characters long");
    }
    if (!cJSON_IsBool(auto_off)) {
        cJSON_Delete(json);
        return send_error(req, 400, "Missing 'auto_off' field");
    }

    wifi_creds_t creds = {0};
    nvs_settings_load_wifi_creds(&creds);
    bool same_ssid = strcmp(creds.ssid, ssid->valuestring) == 0;
    strncpy(creds.ssid, ssid->valuestring, sizeof(creds.ssid) - 1);
    if (cJSON_IsString(password) && password->valuestring[0] != '\0') {
        size_t pass_len = strlen(password->valuestring);
        if (pass_len < 8 || pass_len >= BP10_WIFI_PASS_MAX_LEN) {
            cJSON_Delete(json);
            return send_error(req, 400, "Wi-Fi password must be 8 to 63 characters long");
        }
        strncpy(creds.password, password->valuestring, sizeof(creds.password) - 1);
    } else if (!same_ssid) {
        creds.password[0] = '\0';
    }

    device_config_t config = {
        .wifi_auto_off = cJSON_IsTrue(auto_off),
        .wifi_setup_timeout_s = BP10_WIFI_SETUP_TIMEOUT_S,
    };
    nvs_settings_load_hostname(config.hostname, sizeof(config.hostname));
    cJSON_Delete(json);

    esp_err_t err = nvs_settings_save_wifi_creds(&creds);
    if (err == ESP_OK) err = nvs_settings_save_config(&config);
    if (err != ESP_OK) return send_error(req, 500, "Unable to save Wi-Fi configuration");

    wifi_manager_configure_auto_off(config.wifi_auto_off,
                                    config.wifi_setup_timeout_s, true);
    wifi_manager_note_user_activity();
    // Do not connect from inside the HTTP handler: AP/STA may change channel
    // immediately and GOT_IP intentionally tears down the setup AP. Either can
    // cut off this response and surface as a misleading browser "Failed to
    // fetch" although credentials were saved successfully.
    err = wifi_manager_schedule_sta_connect(creds.ssid, creds.password, 1500);
    if (err != ESP_OK) return send_error(req, 500, "Unable to schedule Wi-Fi connection attempt");

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "auto_off", config.wifi_auto_off);
    char hostname[32];
    wifi_manager_get_hostname(hostname, sizeof(hostname));
    char mdns_addr[48];
    snprintf(mdns_addr, sizeof(mdns_addr), "http://%s.local", hostname);
    cJSON_AddStringToObject(result, "message", "Saved; connecting shortly");
    cJSON_AddStringToObject(result, "mdns_address", mdns_addr);
    cJSON_AddNumberToObject(result, "connect_delay_ms", 1500);
    return send_ok(req, result);
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

static esp_err_t handler_device_name_post(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return send_error(req, 400, "No data");
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) return send_error(req, 400, "Invalid JSON");

    cJSON *name = cJSON_GetObjectItem(json, "hostname");
    if (!cJSON_IsString(name) || strlen(name->valuestring) < 1) {
        cJSON_Delete(json);
        return send_error(req, 400, "Missing 'hostname' field");
    }

    nvs_settings_save_hostname(name->valuestring);
    mdns_service_update_hostname(name->valuestring);
    cJSON_Delete(json);

    return send_ok(req, NULL);
}

static esp_err_t handler_device_reset_post(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Factory Reset – NVS löschen (kein DSP-Flash-Save)");
    nvs_settings_factory_reset();
    // Kein 0xFD an den DSP senden!
    wifi_manager_deinit();
    mdns_service_stop();
    usb_host_ctrl_deinit();
    esp_restart();
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// OTA-Status
// ---------------------------------------------------------------------------

static esp_err_t handler_ota_status_get(httpd_req_t *req)
{
    char *json = ota_get_status_json();
    if (!json) {
        return send_error(req, 500, "OTA status unavailable");
    }
    esp_err_t ret = send_json_response(req, 200, json);
    free(json);
    return ret;
}

// ---------------------------------------------------------------------------
// OTA-Upload (Binary Stream)
// ---------------------------------------------------------------------------

static esp_err_t handler_ota_upload_post(httpd_req_t *req)
{
    // Prüfen ob bereits ein OTA-Vorgang läuft
    if (ota_is_busy()) {
        return send_error(req, 409, "OTA already in progress");
    }

    // Content-Length aus Header lesen
    char content_length_str[32];
    bool has_length = false;
    size_t content_length = 0;
    if (httpd_req_get_hdr_value_str(req, "Content-Length",
                                     content_length_str, sizeof(content_length_str)) == ESP_OK) {
        content_length = (size_t)strtoul(content_length_str, NULL, 10);
        has_length = (content_length > 0);
    }

    if (!has_length) {
        return send_error(req, 411, "Content-Length required");
    }

    ESP_LOGI(TAG, "OTA-Upload: Content-Length=%zu", content_length);

    // Upload starten
    esp_err_t err = ota_upload_begin(content_length);
    if (err != ESP_OK) {
        ota_status_t st;
        ota_get_status(&st);
        char msg[256];
        snprintf(msg, sizeof(msg), "OTA begin failed: %s",
                 st.last_error[0] ? st.last_error : "unknown error");
        return send_error(req, 400, msg);
    }

    // Daten in Blöcken empfangen und schreiben
    // HTTP-Server-Puffer ist typischerweise 512-1024 Bytes
    size_t remaining = content_length;
    uint8_t buf[1024];

    while (remaining > 0) {
        size_t to_read = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
        int received = httpd_req_recv(req, (char *)buf, (int)to_read);
        if (received <= 0) {
            ESP_LOGW(TAG, "OTA-Upload: Empfang abgebrochen nach %zu/%zu Bytes",
                     content_length - remaining, content_length);
            ota_upload_abort();
            if (received == 0) {
                return send_error(req, 400, "Upload aborted (connection closed)");
            }
            return send_error(req, 500, "Upload aborted (recv error)");
        }

        err = ota_upload_write(buf, (size_t)received);
        if (err != ESP_OK) {
            ota_upload_abort();
            return send_error(req, 500, "OTA write failed");
        }

        remaining -= (size_t)received;
    }

    // Upload abschließen
    err = ota_upload_finish();
    if (err != ESP_OK) {
        ota_status_t st;
        ota_get_status(&st);
        char msg[256];
        snprintf(msg, sizeof(msg), "OTA validation failed: %s",
                 st.last_error[0] ? st.last_error : esp_err_to_name(err));
        return send_error(req, 400, msg);
    }

    // Erfolg melden – System startet in 2s neu
    ota_status_t st;
    ota_get_status(&st);
    cJSON *extra = cJSON_CreateObject();
    cJSON_AddStringToObject(extra, "message", "Update successful – rebooting");
    cJSON_AddStringToObject(extra, "new_version", st.uploaded_version);
    cJSON_AddStringToObject(extra, "target_partition", st.target_partition_label);
    cJSON_AddNumberToObject(extra, "reboot_ms", 2000);

    char *resp = cJSON_PrintUnformatted(extra);
    cJSON_Delete(extra);
    esp_err_t ret = send_json_response(req, 200, resp);
    free(resp);

    // Verzögerten Neustart starten
    ESP_LOGI(TAG, "OTA-Update vollständig – Neustart in 2 Sekunden");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ret; // unreachable
}

// ---------------------------------------------------------------------------
// Registrierung
// ---------------------------------------------------------------------------

void api_handlers_register(http_server_handle_t server)
{
    if (!server) return;

    // Static Content
    http_server_register_static_handlers(server);

    httpd_uri_t uris[] = {
        {.uri = "/api/status",       .method = HTTP_GET,  .handler = handler_status_get,       .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/dsp",          .method = HTTP_GET,  .handler = handler_dsp_get,          .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/dsp/noise",    .method = HTTP_POST, .handler = handler_dsp_noise_post,   .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/dsp/bass",     .method = HTTP_POST, .handler = handler_dsp_bass_post,    .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/dsp/silence",  .method = HTTP_POST, .handler = handler_dsp_silence_post, .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/dsp/preeq",    .method = HTTP_POST, .handler = handler_dsp_preeq_post,   .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/dsp/drc",      .method = HTTP_POST, .handler = handler_dsp_drc_post,     .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/dsp/apply",    .method = HTTP_POST, .handler = handler_dsp_apply_post,   .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/dsp/config/export", .method = HTTP_POST, .handler = handler_config_export_post, .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/dsp/config/import", .method = HTTP_POST, .handler = handler_config_import_post, .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/wifi/status",  .method = HTTP_GET,  .handler = handler_wifi_status_get,  .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/wifi/config",  .method = HTTP_GET,  .handler = handler_wifi_config_get,  .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/wifi/config",  .method = HTTP_POST, .handler = handler_wifi_config_post, .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/wifi/scan",    .method = HTTP_POST, .handler = handler_wifi_scan_post,   .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/wifi/scan",    .method = HTTP_GET,  .handler = handler_wifi_scan_get,    .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/device/name",  .method = HTTP_POST, .handler = handler_device_name_post, .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/device/reset", .method = HTTP_POST, .handler = handler_device_reset_post, .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/ota/upload",   .method = HTTP_POST, .handler = handler_ota_upload_post,  .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/ota/status",    .method = HTTP_GET,  .handler = handler_ota_status_get,   .user_ctx = NULL, .is_websocket = false},
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "URI %s registrieren fehlgeschlagen: %s",
                     uris[i].uri, esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "API-Handler registriert (%d Endpunkte)",
             (int)(sizeof(uris) / sizeof(uris[0])));
}
