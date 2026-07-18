// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// mock_usb_transport.c — Mock-USB-Transport für Tests
//

#include "mock_usb_transport.h"
#include "usb_host_ctrl.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "bp10_test_mock";

static uint8_t s_response[256];
static uint16_t s_response_len = 0;
static uint8_t s_last_send[256];
static uint16_t s_last_send_len = 0;
static bool s_installed = false;
static bool s_device_connected = true;

// Überschreibt usb_host_ctrl_send_report
static esp_err_t mock_send_report(const uint8_t *data, uint16_t length)
{
    memcpy(s_last_send, data, length > 256 ? 256 : length);
    s_last_send_len = length > 256 ? 256 : length;

    ESP_LOGD(TAG, "Mock-SEND: %d Bytes", length);
    for (uint16_t i = 0; i < length && i < 16; i++) {
        ESP_LOGD(TAG, "  [%d] 0x%02X", i, data[i]);
    }

    return ESP_OK;
}

// Überschreibt usb_host_ctrl_get_report
static esp_err_t mock_get_report(uint8_t *buffer, uint16_t *out_length)
{
    if (s_response_len > 0) {
        memcpy(buffer, s_response, s_response_len);
        if (out_length) *out_length = s_response_len;
        ESP_LOGD(TAG, "Mock-GET: %d Bytes", s_response_len);
    } else {
        if (out_length) *out_length = 0;
        ESP_LOGW(TAG, "Mock-GET: keine Antwort gesetzt");
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Öffentliche API
// ---------------------------------------------------------------------------

void mock_usb_transport_init(void)
{
    memset(s_response, 0, sizeof(s_response));
    s_response_len = 0;
    memset(s_last_send, 0, sizeof(s_last_send));
    s_last_send_len = 0;
    s_device_connected = true;
    ESP_LOGI(TAG, "Mock-USB initialisiert");
}

void mock_usb_transport_deinit(void)
{
    s_response_len = 0;
    s_last_send_len = 0;
    ESP_LOGI(TAG, "Mock-USB deinitialisiert");
}

void mock_usb_set_response(const uint8_t *data, uint16_t length)
{
    if (length > 256) length = 256;
    memcpy(s_response, data, length);
    s_response_len = length;
}

void mock_usb_get_last_send(uint8_t *data, uint16_t *length)
{
    if (data) memcpy(data, s_last_send, s_last_send_len);
    if (length) *length = s_last_send_len;
}

esp_err_t mock_usb_transport_install(void)
{
    if (s_installed) return ESP_OK;
    s_installed = true;
    mock_usb_transport_init();
    ESP_LOGI(TAG, "Mock-USB-Transport installiert");
    return ESP_OK;
}

void mock_usb_transport_uninstall(void)
{
    s_installed = false;
    mock_usb_transport_deinit();
    ESP_LOGI(TAG, "Mock-USB-Transport deinstalliert");
}