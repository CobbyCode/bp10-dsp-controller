// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// mdns_service.c — mDNS-Dienst
//

#include "mdns_service.h"
#include "app_config.h"
#include <string.h>
#include "esp_log.h"
#include "mdns.h"

static const char *TAG = "bp10_mdns";

esp_err_t mdns_service_start(const char *hostname)
{
    if (!hostname) return ESP_ERR_INVALID_ARG;

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(hostname));

    // Standard-MDNS-Instanz
    char instance[64];
    snprintf(instance, sizeof(instance), "BP10 DSP Controller (%s)", hostname);
    ESP_ERROR_CHECK(mdns_instance_name_set(instance));

    // HTTP-Service registrieren
    ESP_ERROR_CHECK(
        mdns_service_add(instance, "_http", "_tcp", BP10_HTTP_PORT, NULL, 0));

    ESP_LOGI(TAG, "mDNS gestartet: %s.local -> %s", hostname, instance);
    return ESP_OK;
}

esp_err_t mdns_service_update_hostname(const char *hostname)
{
    if (!hostname) return ESP_ERR_INVALID_ARG;

    ESP_ERROR_CHECK(mdns_hostname_set(hostname));

    char instance[64];
    snprintf(instance, sizeof(instance), "BP10 DSP Controller (%s)", hostname);
    ESP_ERROR_CHECK(mdns_instance_name_set(instance));

    ESP_LOGI(TAG, "mDNS aktualisiert: %s.local", hostname);
    return ESP_OK;
}

void mdns_service_stop(void)
{
    mdns_free();
    ESP_LOGI(TAG, "mDNS gestoppt");
}