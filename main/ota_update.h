// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// ota_update.h — Lokales OTA-Firmwareupdate mit Rollback
//
// Features:
//   - POST /api/ota/upload: Firmware als Binary-Stream hochladen
//   - GET  /api/ota/status: Fortschritt und Status abfragen
//   - Image-Validierung (Header, Chip, Projektname, Größe)
//   - Rollback mit Selbsttest (esp_ota_mark_app_valid_cancel_rollback)
//   - Keine gleichzeitigen OTA/Factory-Reset/NVS-Schreibaktionen
//

#pragma once

#include "esp_err.h"
#include "esp_ota_ops.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// OTA-Status
// ---------------------------------------------------------------------------

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_RECEIVING,
    OTA_STATE_VALIDATING,
    OTA_STATE_READY_TO_REBOOT,
    OTA_STATE_FAILED,
} ota_state_t;

typedef struct {
    ota_state_t state;
    size_t      received_bytes;
    size_t      total_bytes;
    int         progress_pct;
    char        target_partition_label[32];
    char        current_version[32];
    char        uploaded_version[32];
    char        uploaded_build_date[32];
    char        uploaded_elf_sha[64];
    char        last_error[128];
    // Rollback/Validierungsstatus der laufenden App
    bool        rollback_pending;   // ESP_OTA_IMG_PENDING_VERIFY
    bool        app_valid;          // ESP_OTA_IMG_VALID
} ota_status_t;

// ---------------------------------------------------------------------------
// Initialisierung
// ---------------------------------------------------------------------------

/**
 * @brief OTA-Subsystem initialisieren.
 *
 * Muss vor dem HTTP-Server-Start aufgerufen werden.
 * Prüft den Rollback-Status der laufenden Partition.
 */
esp_err_t ota_init(void);

// ---------------------------------------------------------------------------
// Upload-Handler (wird vom HTTP-Server aufgerufen)
// ---------------------------------------------------------------------------

/**
 * @brief OTA-Upload starten.
 *
 * Validiert Content-Length gegen Partitionsgröße,
 * beginnt esp_ota_begin() auf der nächsten inaktiven Partition.
 *
 * @param content_length Erwartete Dateigröße in Bytes (aus Content-Length Header)
 * @return ESP_OK bei Erfolg, sonst Fehler
 */
esp_err_t ota_upload_begin(size_t content_length);

/**
 * @brief Einen Block Rohdaten in die OTA-Partition schreiben.
 *
 * @param data Zeiger auf Rohdaten
 * @param len Länge der Daten
 * @return ESP_OK bei Erfolg, ESP_ERR_INVALID_STATE falls nicht im RECEIVING-State
 */
esp_err_t ota_upload_write(const uint8_t *data, size_t len);

/**
 * @brief OTA-Upload abschließen.
 *
 * Prüft Image-Header, validiert Chip/Projektname,
 * setzt Boot-Partition und startet Neustart-Timer.
 *
 * @return ESP_OK bei Erfolg (System startet nach kurzer Verzögerung neu)
 */
esp_err_t ota_upload_finish(void);

/**
 * @brief OTA-Upload abbrechen (z. B. bei Verbindungsabbruch).
 */
void ota_upload_abort(void);

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

/**
 * @brief Aktuellen OTA-Status abfragen (thread-safe).
 *
 * @param status Zeiger auf ota_status_t-Struktur
 * @return ESP_OK
 */
esp_err_t ota_get_status(ota_status_t *status);

/**
 * @brief OTA-Status als JSON-string abfragen.
 *
 * Der Aufrufer ist für das Freigeben des Strings verantwortlich (free()).
 *
 * @return JSON-String (muss mit free() freigegeben werden), oder NULL
 */
char *ota_get_status_json(void);

// ---------------------------------------------------------------------------
// Locking (für Factory-Reset, Config-IO)
// ---------------------------------------------------------------------------

/**
 * @brief Prüfen ob ein OTA-Vorgang aktiv ist.
 *
 * @return true wenn OTA läuft (nicht IDLE/FAILED)
 */
bool ota_is_busy(void);

// ---------------------------------------------------------------------------
// Rollback
// ---------------------------------------------------------------------------

/**
 * @brief Rollback-Selbsttest durchführen.
 *
 * Wird beim Booten aufgerufen, wenn die laufende Partition
 * ESP_OTA_IMG_PENDING_VERIFY ist.
 *
 * Bei Erfolg: esp_ota_mark_app_valid_cancel_rollback()
 * Bei Fehler: esp_ota_mark_app_invalid_rollback_and_reboot()
 */
void ota_perform_self_test(void);

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------

/** Aktuelle Firmware-Version (wird in CMakeLists.txt/project() gesetzt) */
#ifndef APP_VERSION
#define APP_VERSION "0.3.5"
#endif

#ifdef __cplusplus
}
#endif
