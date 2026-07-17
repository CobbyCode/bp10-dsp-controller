// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// ota_update.c — OTA-Firmwareupdate
//

#include "ota_update.h"
#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"

static const char *TAG = "a800x_ota";
static bool s_ota_running = false;

// ---------------------------------------------------------------------------
// HTTP-OTA-Event-Handler
// ---------------------------------------------------------------------------

// OTA-Event-Handler (optional, für später vorgesehen)
// static void ota_event_handler(void *arg, esp_event_base_t event_base,
//                               int32_t event_id, void *event_data)
// {
//     // ESP_HTTPS_OTA-Events können hier geloggt werden
// }

// ---------------------------------------------------------------------------
// Öffentliche API
// ---------------------------------------------------------------------------

esp_err_t ota_update_start(const char *url)
{
    if (!url) return ESP_ERR_INVALID_ARG;
    if (s_ota_running) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "OTA-Update starten: %s", url);

    s_ota_running = true;

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 60000,
        .keep_alive_enable = false,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_err_t err = esp_https_ota(&ota_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA-Update fehlgeschlagen: %s", esp_err_to_name(err));
        s_ota_running = false;
        return err;
    }

    ESP_LOGI(TAG, "OTA-Update erfolgreich. Neustart...");
    s_ota_running = false;

    // System neustarten mit der neuen Firmware
    esp_restart();

    return ESP_OK; // unreachable
}

bool ota_update_is_running(void)
{
    return s_ota_running;
}

void ota_set_version(const char *version)
{
    // Version wird im OTA-Partitions-Label gespeichert
    // (aktuell nur Log)
    ESP_LOGI(TAG, "Firmware-Version: %s", version);
}