// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// web_ui.h — Eingebettete Web-UI-Assets
//
// Die statischen Web-UI-Dateien (HTML, CSS, JS) werden via CMake
// (EMBED_FILES) direkt in die Firmware eingebettet.
// Dieses Modul verwaltet die Übergabe und stellt ggf. dynamische
// Injektionen bereit (z. B. Hostname, IP).
//

#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Zeiger auf eingebettete Datei und deren Größe abrufen.
 *
 * @param path Dateipfad (relativ zu www/)
 * @param[out] data Zeiger auf Dateiinhalt
 * @param[out] size Dateigröße
 * @return ESP_OK bei Erfolg
 */
esp_err_t web_ui_get_file(const char *path, const uint8_t **data, size_t *size);

/**
 * @brief Liste der verfügbaren Web-UI-Dateien.
 *
 * @return NULL-terminiertes String-Array
 */
const char **web_ui_get_file_list(void);

#ifdef __cplusplus
}
#endif