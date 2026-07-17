// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// api_handlers.c — REST-API-Endpunkte
//
// Endpunkte:
//   GET  /api/status          – Systemstatus (DSP, WiFi, IP)
//   GET  /api/dsp             – Aktuellen DSP-Zustand abrufen
//   POST /api/dsp/noise       – Noise Suppressor ein/aus
//   POST /api/dsp/bass        – Virtual Bass ein/aus
//   POST /api/dsp/silence     – Silence Detector ein/aus
//   POST /api/dsp/preeq       – PreEQ-Block ein/aus
//   POST /api/dsp/drc         – DRC-Block ein/aus
//   POST /api/dsp/apply       – Vollständiges Profil anwenden
//   POST /api/dsp/readback    – DSP-Zustand vom Gerät lesen
//   POST /api/dsp/preeq/set   – PreEQ-Filter setzen
//   POST /api/dsp/drc/set     – DRC setzen
//   GET  /api/wifi/status     – WiFi-Status
//   POST /api/wifi/connect    – WiFi-Verbindung herstellen
//   POST /api/wifi/ap/start   – SoftAP starten
//   GET  /api/profiles        – Gespeicherte Profile auflisten
//   POST /api/profiles/save   – Profil speichern
//   POST /api/profiles/load   – Profil laden
//   POST /api/config/export   – Konfiguration exportieren (JSON)
//   POST /api/config/import   – Konfiguration importieren (JSON)
//   POST /api/ota/update      – OTA-Update starten
//   POST /api/device/name     – Gerätenamen setzen
//   POST /api/device/reset    – Factory Reset
//

#include "api_handlers.h"
#include "dsp_model.h"
#include "nvs_settings.h"
#include "wifi_manager.h"
#include "mdns_service.h"
#include "ota_update.h"
#include "config_io.h"
#include "usb_host_ctrl.h"

#include <string.h>
#include <cJSON.h>
#include "esp_log.h"

static const char *TAG = "a800x_api";

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

// ---------------------------------------------------------------------------
// Handler
// ---------------------------------------------------------------------------

static esp_err_t handler_status_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "running");
    cJSON_AddStringToObject(root, "version", APP_VERSION);

    // DSP
    cJSON_AddBoolToObject(root, "dsp_connected",
                          usb_host_ctrl_is_device_connected());

    // WiFi
    char ip[16];
    cJSON_AddBoolToObject(root, "wifi_connected",
                          wifi_manager_is_connected());
    if (wifi_manager_get_ip_str(ip, sizeof(ip)) == ESP_OK) {
        cJSON_AddStringToObject(root, "ip", ip);
    }

    char mac[18];
    wifi_manager_get_mac_str(mac);
    cJSON_AddStringToObject(root, "mac", mac);

    char hostname[32];
    wifi_manager_generate_hostname(hostname, sizeof(hostname));
    cJSON_AddStringToObject(root, "hostname", hostname);

    char *json = cJSON_PrintUnformatted(root);
    esp_err_t ret = send_json_response(req, 200, json);
    free(json);
    cJSON_Delete(root);
    return ret;
}

static esp_err_t handler_dsp_get(httpd_req_t *req)
{
    dsp_profile_t profile;
    esp_err_t err = dsp_model_readback(&profile);
    if (err != ESP_OK) {
        return send_error(req, 500, "DSP-Readback fehlgeschlagen");
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "noise_suppressor", profile.noise_suppressor_enabled);
    cJSON_AddBoolToObject(root, "virtual_bass", profile.virtual_bass_enabled);
    cJSON_AddBoolToObject(root, "silence_detector", profile.silence_detector_enabled);
    cJSON_AddBoolToObject(root, "preeq_enabled", profile.preeq.block_enabled != 0);
    cJSON_AddBoolToObject(root, "drc_enabled", profile.drc.enabled != 0);

    cJSON_AddNumberToObject(root, "preeq_pre_gain_raw", profile.preeq.pre_gain_raw);

    // PreEQ-Filter als Array
    cJSON *filters = cJSON_AddArrayToObject(root, "preeq_filters");
    for (int i = 0; i < 10; i++) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddBoolToObject(f, "enabled", profile.preeq.filters[i].enabled != 0);
        cJSON_AddNumberToObject(f, "type", profile.preeq.filters[i].type);
        cJSON_AddNumberToObject(f, "frequency_hz", profile.preeq.filters[i].frequency_hz);
        cJSON_AddNumberToObject(f, "q_raw", profile.preeq.filters[i].q_raw);
        cJSON_AddNumberToObject(f, "gain_raw", profile.preeq.filters[i].gain_raw);
        cJSON_AddItemToArray(filters, f);
    }

    char *json = cJSON_PrintUnformatted(root);
    esp_err_t ret = send_json_response(req, 200, json);
    free(json);
    cJSON_Delete(root);
    return ret;
}

static esp_err_t handler_dsp_noise_post(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return send_error(req, 400, "Keine Daten");
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) return send_error(req, 400, "Ungültiges JSON");

    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    if (!cJSON_IsBool(enable)) {
        cJSON_Delete(json);
        return send_error(req, 400, "Feld 'enable' fehlt");
    }

    esp_err_t err = dsp_model_set_noise_suppressor(cJSON_IsTrue(enable));
    cJSON_Delete(json);

    if (err != ESP_OK) {
        return send_error(req, 500, "Noise Suppressor setzen fehlgeschlagen");
    }
    return send_ok(req, NULL);
}

static esp_err_t handler_dsp_bass_post(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return send_error(req, 400, "Keine Daten");
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) return send_error(req, 400, "Ungültiges JSON");

    cJSON *enable = cJSON_GetObjectItem(json, "enable");
    if (!cJSON_IsBool(enable)) {
        cJSON_Delete(json);
        return send_error(req, 400, "Feld 'enable' fehlt");
    }

    esp_err_t err = dsp_model_set_virtual_bass(cJSON_IsTrue(enable));
    cJSON_Delete(json);

    if (err != ESP_OK) {
        return send_error(req, 500, "Virtual Bass setzen fehlgeschlagen");
    }
    return send_ok(req, NULL);
}

static esp_err_t handler_wifi_status_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    char ip[16];
    cJSON_AddBoolToObject(root, "connected", wifi_manager_is_connected());
    if (wifi_manager_get_ip_str(ip, sizeof(ip)) == ESP_OK) {
        cJSON_AddStringToObject(root, "ip", ip);
    }
    char *json = cJSON_PrintUnformatted(root);
    esp_err_t err = send_json_response(req, 200, json);
    free(json);
    cJSON_Delete(root);
    return err;
}

static esp_err_t handler_device_name_post(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return send_error(req, 400, "Keine Daten");
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) return send_error(req, 400, "Ungültiges JSON");

    cJSON *name = cJSON_GetObjectItem(json, "hostname");
    if (!cJSON_IsString(name) || strlen(name->valuestring) < 1) {
        cJSON_Delete(json);
        return send_error(req, 400, "Feld 'hostname' fehlt");
    }

    nvs_settings_save_hostname(name->valuestring);
    mdns_service_update_hostname(name->valuestring);
    cJSON_Delete(json);

    return send_ok(req, NULL);
}

static esp_err_t handler_device_reset_post(httpd_req_t *req)
{
    nvs_settings_factory_reset();
    wifi_manager_deinit();
    mdns_service_stop();
    usb_host_ctrl_deinit();
    // System neustarten (vereinfacht)
    ESP_LOGI(TAG, "Factory Reset – neustarten...");
    esp_restart();
    return ESP_OK; // unreachable
}

static esp_err_t handler_ota_update_post(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return send_error(req, 400, "Keine Daten");
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) return send_error(req, 400, "Ungültiges JSON");

    cJSON *url = cJSON_GetObjectItem(json, "url");
    if (!cJSON_IsString(url)) {
        cJSON_Delete(json);
        return send_error(req, 400, "Feld 'url' fehlt");
    }

    esp_err_t err = ota_update_start(url->valuestring);
    cJSON_Delete(json);

    if (err != ESP_OK) {
        return send_error(req, 500, "OTA-Update fehlgeschlagen");
    }
    return send_ok(req, NULL);
}

// ---------------------------------------------------------------------------
// Registrierung
// ---------------------------------------------------------------------------

void api_handlers_register(http_server_handle_t server)
{
    if (!server) return;

    // Static Content
    http_server_register_static_handlers(server);

    // API-Endpunkte
    httpd_uri_t uris[] = {
        {.uri = "/api/status",      .method = HTTP_GET,  .handler = handler_status_get,      .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/dsp",         .method = HTTP_GET,  .handler = handler_dsp_get,         .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/dsp/noise",   .method = HTTP_POST, .handler = handler_dsp_noise_post,  .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/dsp/bass",    .method = HTTP_POST, .handler = handler_dsp_bass_post,   .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/wifi/status", .method = HTTP_GET,  .handler = handler_wifi_status_get, .user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/device/name", .method = HTTP_POST, .handler = handler_device_name_post,.user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/device/reset",.method = HTTP_POST, .handler = handler_device_reset_post,.user_ctx = NULL, .is_websocket = false},
        {.uri = "/api/ota/update",  .method = HTTP_POST, .handler = handler_ota_update_post, .user_ctx = NULL, .is_websocket = false},
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