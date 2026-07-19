// SPDX-FileCopyrightText: 2026 CobbyCode
// SPDX-License-Identifier: GPL-3.0-or-later
//
// web_ui.c — Eingebettete Web-UI-Assets
//

#include "web_ui.h"
#include <string.h>

// Die eingebetteten Dateien werden durch CMake EMBED_FILES als
// Symbole bereitgestellt. Deklaration in http_server.c.
// web_ui.c ist primär ein Platzhalter für spätere Logik
// (z. B. dynamische HTML-Injektion).

esp_err_t web_ui_get_file(const char *path, const uint8_t **data, size_t *size)
{
    if (!path || !data || !size) return ESP_ERR_INVALID_ARG;

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        extern const uint8_t index_html_start[] asm("_binary_index_html_start");
        extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
        *data = index_html_start;
        *size = index_html_end - index_html_start;
        return ESP_OK;
    }

    if (strcmp(path, "/style.css") == 0) {
        extern const uint8_t style_css_start[] asm("_binary_style_css_start");
        extern const uint8_t style_css_end[]   asm("_binary_style_css_end");
        *data = style_css_start;
        *size = style_css_end - style_css_start;
        return ESP_OK;
    }

    if (strcmp(path, "/app.js") == 0) {
        extern const uint8_t app_js_start[] asm("_binary_app_js_start");
        extern const uint8_t app_js_end[]   asm("_binary_app_js_end");
        *data = app_js_start;
        *size = app_js_end - app_js_start;
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

const char **web_ui_get_file_list(void)
{
    static const char *files[] = {
        "/index.html",
        "/style.css",
        "/app.js",
        NULL
    };
    return files;
}
