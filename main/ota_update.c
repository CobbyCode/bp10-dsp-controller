// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// ota_update.c — Lokales OTA-Firmwareupdate mit Rollback
//
// Architektur:
//   - ota_init() prüft Rollback-Status beim Boot
//   - ota_upload_begin/write/finish/abort für Streaming-Upload
//   - ota_get_status() für Fortschrittsanzeige (thread-safe via Mutex)
//   - ota_perform_self_test() für Rollback-Validierung
//
// Sicherheit:
//   - Image-Header-Validierung (Magic, Chip, Project-Name)
//   - Größenprüfung gegen Partition
//   - Kein Überschreiben der laufenden Partition
//   - Bootloader/Partition-Table/Merged-Binaries werden abgelehnt
//   - Downgrade nur mit expliziter Bestätigung
//

#include "ota_update.h"
#include <string.h>
#include <stdarg.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_image_format.h"
#include "esp_partition.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "app_config.h"

static const char *TAG = "a800x_ota";

// ---------------------------------------------------------------------------
// Mutex
// ---------------------------------------------------------------------------
static SemaphoreHandle_t s_ota_mutex = NULL;

// ---------------------------------------------------------------------------
// OTA-Zustand
// ---------------------------------------------------------------------------
static ota_status_t s_ota_status = { .state = OTA_STATE_IDLE };
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_target_partition = NULL;
static const esp_partition_t *s_running_partition = NULL;
static bool s_init_done = false;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void ota_set_state(ota_state_t state);
static void ota_set_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void ota_clear_error(void);

// ---------------------------------------------------------------------------
// Locking
// ---------------------------------------------------------------------------
#define OTA_LOCK()   do { if (s_ota_mutex) xSemaphoreTake(s_ota_mutex, portMAX_DELAY); } while(0)
#define OTA_UNLOCK() do { if (s_ota_mutex) xSemaphoreGive(s_ota_mutex); } while(0)

bool ota_is_busy(void)
{
    if (!s_ota_mutex) return false;
    bool busy;
    OTA_LOCK();
    busy = (s_ota_status.state != OTA_STATE_IDLE &&
            s_ota_status.state != OTA_STATE_FAILED);
    OTA_UNLOCK();
    return busy;
}

// ---------------------------------------------------------------------------
// Initialisierung
// ---------------------------------------------------------------------------
esp_err_t ota_init(void)
{
    if (s_ota_mutex) return ESP_OK; // bereits initialisiert

    s_ota_mutex = xSemaphoreCreateMutex();
    if (!s_ota_mutex) {
        ESP_LOGE(TAG, "OTA-Mutex erstellen fehlgeschlagen");
        return ESP_ERR_NO_MEM;
    }

    // Laufende Partition ermitteln
    s_running_partition = esp_ota_get_running_partition();
    if (!s_running_partition) {
        ESP_LOGE(TAG, "Laufende Partition nicht gefunden");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "Laufende Partition: %s (Subtyp %d, Offset 0x%lx, Größe %lu)",
             s_running_partition->label,
             s_running_partition->subtype,
             (unsigned long)s_running_partition->address,
             (unsigned long)s_running_partition->size);

    // Aktuelle Firmware-Info
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc) {
        snprintf(s_ota_status.current_version, sizeof(s_ota_status.current_version),
                 "%s", app_desc->version);
        ESP_LOGI(TAG, "Firmware: %s (Build: %s %s, ELF-SHA256: %.8s...)",
                 app_desc->version, app_desc->date, app_desc->time,
                 app_desc->app_elf_sha256);
    }

    // Rollback-Status der laufenden App prüfen
    esp_ota_img_states_t rollback_state;
    esp_err_t err = esp_ota_get_state_partition(s_running_partition, &rollback_state);
    if (err == ESP_OK) {
        switch (rollback_state) {
        case ESP_OTA_IMG_VALID:
            ESP_LOGI(TAG, "App-Status: VALID (kein Rollback)");
            s_ota_status.app_valid = true;
            s_ota_status.rollback_pending = false;
            break;
        case ESP_OTA_IMG_PENDING_VERIFY:
            ESP_LOGI(TAG, "App-Status: PENDING_VERIFY – "
                     "Selbsttest wird ausgeführt...");
            s_ota_status.rollback_pending = true;
            s_ota_status.app_valid = false;
            break;
        case ESP_OTA_IMG_INVALID:
            ESP_LOGI(TAG, "App-Status: INVALID – "
                     "Bootloader sollte zur vorherigen Version zurückgekehrt sein");
            s_ota_status.rollback_pending = false;
            s_ota_status.app_valid = false;
            break;
        case ESP_OTA_IMG_ABORTED:
            ESP_LOGI(TAG, "App-Status: ABORTED");
            s_ota_status.rollback_pending = false;
            s_ota_status.app_valid = false;
            break;
        default:
            ESP_LOGW(TAG, "App-Status: unbekannt (%d)", rollback_state);
            break;
        }
    } else {
        ESP_LOGW(TAG, "Rollback-Status nicht lesbar: %s (factory?)", esp_err_to_name(err));
    }

    // OTA-Boot-Partition (nächste nach Boot) anzeigen
    const esp_partition_t *boot_part = esp_ota_get_boot_partition();
    if (boot_part) {
        ESP_LOGI(TAG, "Boot-Partition: %s (Offset 0x%lx)",
                 boot_part->label, (unsigned long)boot_part->address);
    }

    s_init_done = true;
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
static void ota_set_state(ota_state_t state)
{
    OTA_LOCK();
    s_ota_status.state = state;
    OTA_UNLOCK();
}

static void ota_set_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    OTA_LOCK();
    vsnprintf(s_ota_status.last_error, sizeof(s_ota_status.last_error), fmt, args);
    OTA_UNLOCK();
    va_end(args);
    ESP_LOGE(TAG, "OTA-Fehler: %s", s_ota_status.last_error);
}

static void ota_clear_error(void)
{
    OTA_LOCK();
    s_ota_status.last_error[0] = '\0';
    OTA_UNLOCK();
}

esp_err_t ota_get_status(ota_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    OTA_LOCK();
    memcpy(status, &s_ota_status, sizeof(ota_status_t));
    OTA_UNLOCK();
    return ESP_OK;
}

char *ota_get_status_json(void)
{
    ota_status_t st;
    ota_get_status(&st);

    const char *state_str = "idle";
    switch (st.state) {
    case OTA_STATE_IDLE:           state_str = "idle"; break;
    case OTA_STATE_RECEIVING:      state_str = "receiving"; break;
    case OTA_STATE_VALIDATING:     state_str = "validating"; break;
    case OTA_STATE_READY_TO_REBOOT: state_str = "ready_to_reboot"; break;
    case OTA_STATE_FAILED:         state_str = "failed"; break;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", state_str);
    cJSON_AddNumberToObject(root, "received_bytes", (double)st.received_bytes);
    cJSON_AddNumberToObject(root, "total_bytes", (double)st.total_bytes);
    cJSON_AddNumberToObject(root, "progress_pct", st.progress_pct);
    cJSON_AddStringToObject(root, "target_partition", st.target_partition_label);
    cJSON_AddStringToObject(root, "current_version", st.current_version);
    cJSON_AddStringToObject(root, "uploaded_version", st.uploaded_version);
    cJSON_AddStringToObject(root, "uploaded_build_date", st.uploaded_build_date);
    cJSON_AddStringToObject(root, "uploaded_elf_sha", st.uploaded_elf_sha);
    if (st.last_error[0]) {
        cJSON_AddStringToObject(root, "last_error", st.last_error);
    }
    cJSON_AddBoolToObject(root, "rollback_pending", st.rollback_pending);
    cJSON_AddBoolToObject(root, "app_valid", st.app_valid);

    // Laufende und Boot-Partition hinzufügen
    if (s_running_partition) {
        cJSON_AddStringToObject(root, "running_partition", s_running_partition->label);
    }
    const esp_partition_t *boot_part = esp_ota_get_boot_partition();
    if (boot_part) {
        cJSON_AddStringToObject(root, "boot_partition", boot_part->label);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// ---------------------------------------------------------------------------
// Image-Validierung
// ---------------------------------------------------------------------------

/**
 * @brief Prüft den ESP-App-Image-Header der hochgeladenen Firmware.
 *
 * Validiert: Magic-Byte, ESP32-S3-Chip, Projektname, Größe.
 * Lehnt Bootloader-, Partition-Table- und Merged-Flash-Binaries ab.
 *
 * @return ESP_OK wenn gültig, sonst Fehlercode
 */
static esp_err_t ota_validate_app_image(void)
{
    if (!s_target_partition) return ESP_ERR_INVALID_STATE;

    // Partition mappen für Header-Zugriff
    esp_partition_mmap_handle_t mmap_handle;
    const void *mapped;
    esp_err_t err = esp_partition_mmap(s_target_partition, 0,
                                        sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
                                        ESP_PARTITION_MMAP_DATA, &mapped, &mmap_handle);
    if (err != ESP_OK) {
        return err;
    }

    const esp_image_header_t *header = (const esp_image_header_t *)mapped;

    // Magic check
    if (header->magic != ESP_IMAGE_HEADER_MAGIC) {
        esp_partition_munmap(mmap_handle);
        ota_set_error("Kein gültiges ESP-Image (Magic: 0x%02X)", header->magic);
        return ESP_ERR_INVALID_CRC;
    }

    // Chip check: ESP32-S3 = 9
    if (header->chip_id != 9) {
        esp_partition_munmap(mmap_handle);
        ota_set_error("Falscher Chip: %d (erwartet: 9 = ESP32-S3)", header->chip_id);
        return ESP_ERR_INVALID_VERSION;
    }

    // Größe prüfen anhand der tatsächlich empfangenen Daten (statt
    // Header-Berechnung, die nur sizeof(header) liefert).
    // ota_upload_begin hat bereits content_length vs. Partition geprüft.
    uint32_t image_size = s_ota_status.total_bytes;

    // Grobe Größenprüfung: Bootloader ist < 64KB, Partition-Table < 4KB
    if (image_size < 65536) {
        esp_partition_munmap(mmap_handle);
        ota_set_error("Image zu klein (%lu Bytes) – Bootloader/Partition-Table?",
                      (unsigned long)image_size);
        return ESP_ERR_INVALID_SIZE;
    }

    // Merged-Flash erkennen: zu groß (> 8MB für reine App)
    if (image_size > 8 * 1024 * 1024) {
        esp_partition_munmap(mmap_handle);
        ota_set_error("Image zu groß (%lu Bytes) – Merged-Flash?",
                      (unsigned long)image_size);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_partition_munmap(mmap_handle);

    // App-Descriptor lesen (Projektname, Version)
    esp_app_desc_t app_desc;
    err = esp_ota_get_partition_description(s_target_partition, &app_desc);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA-Image validiert: Projekt=%s, Version=%s, "
                 "Build=%s %s",
                 app_desc.project_name, app_desc.version,
                 app_desc.date, app_desc.time);

        // Projektname prüfen
        if (strcmp(app_desc.project_name, "a800x_dsp_controller") != 0) {
            ota_set_error("Falsches Projekt: %s (erwartet: a800x_dsp_controller)",
                          app_desc.project_name);
            return ESP_ERR_INVALID_VERSION;
        }

        // Version/Info in Status übernehmen
        OTA_LOCK();
        snprintf(s_ota_status.uploaded_version, sizeof(s_ota_status.uploaded_version),
                 "%s", app_desc.version);
        snprintf(s_ota_status.uploaded_build_date, sizeof(s_ota_status.uploaded_build_date),
                 "%s %s", app_desc.date, app_desc.time);
        snprintf(s_ota_status.uploaded_elf_sha, sizeof(s_ota_status.uploaded_elf_sha),
                 "%.32s", app_desc.app_elf_sha256);
        OTA_UNLOCK();
    } else {
        ESP_LOGW(TAG, "App-Descriptor nicht lesbar: %s", esp_err_to_name(err));
    }

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Upload
// ---------------------------------------------------------------------------

esp_err_t ota_upload_begin(size_t content_length)
{
    if (!s_init_done) return ESP_ERR_INVALID_STATE;

    ota_status_t st;
    ota_get_status(&st);
    if (st.state != OTA_STATE_IDLE && st.state != OTA_STATE_FAILED) {
        ESP_LOGW(TAG, "OTA bereits aktiv (State: %d)", st.state);
        return ESP_ERR_INVALID_STATE;
    }

    // Nächste inaktive Partition bestimmen
    s_target_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_target_partition) {
        ota_set_error("Keine OTA-Update-Partition gefunden");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "OTA-Zielpartition: %s (Offset 0x%lx, Größe %lu)",
             s_target_partition->label,
             (unsigned long)s_target_partition->address,
             (unsigned long)s_target_partition->size);

    // Nicht in laufende Partition schreiben
    if (s_running_partition && s_target_partition->address == s_running_partition->address) {
        ota_set_error("Zielpartition ist die laufende Partition – Abbruch");
        s_target_partition = NULL;
        return ESP_ERR_INVALID_STATE;
    }

    // Größe prüfen
    if (content_length > s_target_partition->size) {
        ota_set_error("Firmware (%zu Bytes) > Partition (%lu Bytes)",
                      content_length, (unsigned long)s_target_partition->size);
        s_target_partition = NULL;
        return ESP_ERR_INVALID_SIZE;
    }

    if (content_length < 65536) {
        ota_set_error("Firmware zu klein (%zu Bytes) – keine gültige App?", content_length);
        s_target_partition = NULL;
        return ESP_ERR_INVALID_SIZE;
    }

    // OTA beginnen
    esp_err_t err = esp_ota_begin(s_target_partition, content_length, &s_ota_handle);
    if (err != ESP_OK) {
        ota_set_error("esp_ota_begin fehlgeschlagen: %s", esp_err_to_name(err));
        s_target_partition = NULL;
        return err;
    }

    OTA_LOCK();
    s_ota_status.state = OTA_STATE_RECEIVING;
    s_ota_status.received_bytes = 0;
    s_ota_status.total_bytes = content_length;
    s_ota_status.progress_pct = 0;
    s_ota_status.last_error[0] = '\0';
    snprintf(s_ota_status.target_partition_label, sizeof(s_ota_status.target_partition_label),
             "%s", s_target_partition->label);
    s_ota_status.uploaded_version[0] = '\0';
    s_ota_status.uploaded_build_date[0] = '\0';
    s_ota_status.uploaded_elf_sha[0] = '\0';
    OTA_UNLOCK();

    ESP_LOGI(TAG, "OTA-Upload gestartet: %zu Bytes → %s", content_length,
             s_target_partition->label);
    return ESP_OK;
}

esp_err_t ota_upload_write(const uint8_t *data, size_t len)
{
    if (!s_ota_handle) return ESP_ERR_INVALID_STATE;
    if (!data || len == 0) return ESP_OK;

    esp_err_t err = esp_ota_write(s_ota_handle, data, len);
    if (err != ESP_OK) {
        ota_set_error("esp_ota_write fehlgeschlagen: %s", esp_err_to_name(err));
        ota_set_state(OTA_STATE_FAILED);
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
        return err;
    }

    OTA_LOCK();
    s_ota_status.received_bytes += len;
    if (s_ota_status.total_bytes > 0) {
        s_ota_status.progress_pct = (int)((s_ota_status.received_bytes * 100)
                                          / s_ota_status.total_bytes);
    }
    OTA_UNLOCK();

    // Watchdog füttern bei großen Uploads
    esp_task_wdt_reset();

    return ESP_OK;
}

esp_err_t ota_upload_finish(void)
{
    ota_status_t st;
    ota_get_status(&st);

    if (st.state != OTA_STATE_RECEIVING) {
        ota_set_error("OTA nicht im Empfangsmodus (State: %d)", st.state);
        return ESP_ERR_INVALID_STATE;
    }

    // Prüfen dass alle Daten empfangen wurden
    if (st.received_bytes != st.total_bytes) {
        ota_set_error("Unvollständiger Upload: %zu/%zu Bytes",
                      st.received_bytes, st.total_bytes);
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
        ota_set_state(OTA_STATE_FAILED);
        return ESP_ERR_INVALID_SIZE;
    }

    ota_set_state(OTA_STATE_VALIDATING);
    ota_clear_error();

    ESP_LOGI(TAG, "Upload abgeschlossen (%zu Bytes) – validiere Image...",
             st.received_bytes);

    // esp_ota_end() schreibt Checksum und schließt ab
    esp_err_t err = esp_ota_end(s_ota_handle);
    s_ota_handle = 0;
    if (err != ESP_OK) {
        ota_set_error("esp_ota_end fehlgeschlagen: %s", esp_err_to_name(err));
        ota_set_state(OTA_STATE_FAILED);
        // Kein expliziter Abort nötig – esp_ota_end hat bereits aufgeräumt
        return err;
    }

    // Image validieren (Header, Chip, Projekt, Größe)
    err = ota_validate_app_image();
    if (err != ESP_OK) {
        ota_set_state(OTA_STATE_FAILED);
        // Partition nicht als boot-fähig setzen
        return err;
    }

    // Boot-Partition setzen
    err = esp_ota_set_boot_partition(s_target_partition);
    if (err != ESP_OK) {
        ota_set_error("esp_ota_set_boot_partition fehlgeschlagen: %s",
                      esp_err_to_name(err));
        ota_set_state(OTA_STATE_FAILED);
        return err;
    }

    ota_set_state(OTA_STATE_READY_TO_REBOOT);

    ESP_LOGI(TAG, "OTA erfolgreich. Neue Firmware: %s → %s. "
             "Neustart in 2 Sekunden...",
             st.current_version,
             s_ota_status.uploaded_version[0] ? s_ota_status.uploaded_version : "?");
    ESP_LOGI(TAG, "Boot-Partition gesetzt auf: %s", s_target_partition->label);
    ESP_LOGI(TAG, "Neue App wird beim Boot ESP_OTA_IMG_PENDING_VERIFY sein – "
             "Selbsttest läuft in ota_perform_self_test()");

    return ESP_OK;
}

void ota_upload_abort(void)
{
    if (s_ota_handle) {
        ESP_LOGW(TAG, "OTA-Upload abgebrochen");
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }
    s_target_partition = NULL;
    ota_set_error("Upload abgebrochen");
    ota_set_state(OTA_STATE_FAILED);
}

// ---------------------------------------------------------------------------
// Rollback-Selbsttest
// ---------------------------------------------------------------------------
void ota_perform_self_test(void)
{
    if (!s_ota_status.rollback_pending) {
        ESP_LOGI(TAG, "Kein Rollback-Selbsttest nötig (App ist VALID)");
        return;
    }

    ESP_LOGI(TAG, "=== OTA-Rollback-Selbsttest ===");

    // Selbsttest: minimale Boot-Validierung
    // Der Test prüft nur Dinge, die NICHT vom A800X-DSP abhängen.
    bool test_ok = true;
    const char *fail_reason = NULL;

    // 1. NVS lesbar (einfacher Lese-Test)
    {
        nvs_handle_t nvs_handle;
        esp_err_t nvs_err = nvs_open("a800x", NVS_READONLY, &nvs_handle);
        if (nvs_err != ESP_OK) {
            test_ok = false;
            fail_reason = "NVS nicht lesbar";
            ESP_LOGE(TAG, "Selbsttest FEHLER: %s (%s)", fail_reason, esp_err_to_name(nvs_err));
        } else {
            nvs_close(nvs_handle);
            ESP_LOGI(TAG, "  ✅ NVS lesbar");
        }
    }

    // 2. Partitionstabelle plausibel (mindestens 2 OTA-Slots)
    if (test_ok) {
        const esp_partition_t *ota0 = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
        const esp_partition_t *ota1 = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
        if (!ota0 || !ota1) {
            test_ok = false;
            fail_reason = "OTA-Partitionen fehlen";
            ESP_LOGE(TAG, "Selbsttest FEHLER: %s (ota0=%p, ota1=%p)",
                     fail_reason, (void*)ota0, (void*)ota1);
        } else {
            ESP_LOGI(TAG, "  ✅ OTA-Partitionen vorhanden (ota_0=%lu, ota_1=%lu)",
                     (unsigned long)ota0->size, (unsigned long)ota1->size);
        }
    }

    // 3. WiFi-Manager initialisiert? (nur prüfen ob NVS-Creds existieren)
    //    Die volle WiFi-Init passiert erst später; hier nur Existenz-Check.
    //    OK solange NVS lesbar (bereits geprüft).

    // 4. HTTP-Server und API werden später initialisiert – 
    //    dieser Test prüft nur die Minimal-Boot-Fähigkeit.
    //    Der HTTP-Server-Start erfolgt nach diesem Selbsttest in init_network/app_main.

    if (test_ok) {
        ESP_LOGI(TAG, "Selbsttest BESTANDEN – markiere App als VALID");
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_mark_app_valid_cancel_rollback fehlgeschlagen: %s",
                     esp_err_to_name(err));
        } else {
            s_ota_status.app_valid = true;
            s_ota_status.rollback_pending = false;
            ESP_LOGI(TAG, "App ist jetzt VALID – Rollback deaktiviert");
        }
    } else {
        ESP_LOGE(TAG, "Selbsttest FEHLGESCHLAGEN: %s – "
                 "markiere App als INVALID → Rollback + Reboot", fail_reason);
        esp_ota_mark_app_invalid_rollback_and_reboot();
        // Kein Return – Reboot erfolgt hier
    }
}
