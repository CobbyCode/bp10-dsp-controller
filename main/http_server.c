// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// http_server.c — HTTP-Server
//

#include "http_server.h"
#include "web_ui.h"
#include "app_config.h"
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"

static const char *TAG = "a800x_http";

// ---------------------------------------------------------------------------
// Static-File-Handler (eingebettete Web-UI)
// ---------------------------------------------------------------------------

static esp_err_t handler_index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");

    extern const uint8_t index_html_start[] asm("_binary_index_html_start");
    extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
    size_t size = index_html_end - index_html_start;

    httpd_resp_send(req, (const char *)index_html_start, size);
    return ESP_OK;
}

static esp_err_t handler_style_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");

    extern const uint8_t style_css_start[] asm("_binary_style_css_start");
    extern const uint8_t style_css_end[]   asm("_binary_style_css_end");
    size_t size = style_css_end - style_css_start;

    httpd_resp_send(req, (const char *)style_css_start, size);
    return ESP_OK;
}

static esp_err_t handler_app_js_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");

    extern const uint8_t app_js_start[] asm("_binary_app_js_start");
    extern const uint8_t app_js_end[]   asm("_binary_app_js_end");
    size_t size = app_js_end - app_js_start;

    httpd_resp_send(req, (const char *)app_js_start, size);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Öffentliche API
// ---------------------------------------------------------------------------

httpd_config_t http_server_get_default_config(uint16_t port)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 20;
    return config;
}

esp_err_t http_server_start(http_server_handle_t *server, uint16_t port)
{
    if (!server) return ESP_ERR_INVALID_ARG;

    httpd_config_t config = http_server_get_default_config(port);

    esp_err_t err = httpd_start(server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP-Server-Start fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "HTTP-Server läuft auf Port %d", port);
    return ESP_OK;
}

void http_server_stop(http_server_handle_t server)
{
    if (server) {
        httpd_stop(server);
        ESP_LOGI(TAG, "HTTP-Server gestoppt");
    }
}

void http_server_register_static_handlers(http_server_handle_t server)
{
    if (!server) return;

    httpd_uri_t uri_index = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = handler_index_get,
        .user_ctx  = NULL,
    };
    httpd_register_uri_handler(server, &uri_index);

    httpd_uri_t uri_style = {
        .uri       = "/style.css",
        .method    = HTTP_GET,
        .handler   = handler_style_get,
        .user_ctx  = NULL,
    };
    httpd_register_uri_handler(server, &uri_style);

    httpd_uri_t uri_app = {
        .uri       = "/app.js",
        .method    = HTTP_GET,
        .handler   = handler_app_js_get,
        .user_ctx  = NULL,
    };
    httpd_register_uri_handler(server, &uri_app);

    ESP_LOGI(TAG, "Static-Handler registriert: /, /style.css, /app.js");
}