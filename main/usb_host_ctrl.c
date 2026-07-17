// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// usb_host_ctrl.c — USB-Host-Controller für MVSilicon-Gerät
//
// Implementiert die USB-Host-Initialisierung, Device-Enumeration
// und HID Control Transfers (SET_REPORT / GET_REPORT) für den ESP32-S3.
//
// Framing:
//   A5 5A <effect-id> <length/command> ... 16
//   HID SET_REPORT via Control Transfer, 256 Bytes
//   bmRequestType: 0x21, bRequest: 0x09, wValue: 0x0200, wIndex: 0x0000
//   GET_REPORT:    0xA1, bRequest: 0x01, wValue: 0x0100, wIndex: 0x0000
//

#include "usb_host_ctrl.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"
#include "driver/gpio.h"

static const char *TAG = "a800x_usb_host";

// ---------------------------------------------------------------------------
// Interner Zustand
// ---------------------------------------------------------------------------
static bool s_initialized = false;
static bool s_device_connected = false;
static usb_device_handle_t s_device_handle = NULL;
static usb_host_client_handle_t s_client_handle = NULL;

// Event-Group für Device-Connect/Disconnect-Sync
static EventGroupHandle_t s_events = NULL;
#define EVENT_DEVICE_CONNECTED    BIT0
#define EVENT_DEVICE_DISCONNECTED BIT1

// Semaphor für synchrone Control-Transfers
static SemaphoreHandle_t s_xfer_sem = NULL;
static esp_err_t s_xfer_result = ESP_OK;
static int s_xfer_actual = 0;

// ---------------------------------------------------------------------------
// Transfer-Callback (wird aus usb_host_client_handle_events() aufgerufen)
// ---------------------------------------------------------------------------
static void transfer_callback(usb_transfer_t *xfer)
{
    s_xfer_result = ESP_OK;
    s_xfer_actual = xfer->actual_num_bytes;

    switch (xfer->status) {
        case USB_TRANSFER_STATUS_COMPLETED:
            ESP_LOGD(TAG, "Transfer completed: %d bytes", xfer->actual_num_bytes);
            break;
        case USB_TRANSFER_STATUS_STALL:
            ESP_LOGW(TAG, "Transfer STALL (Gerät abgelehnt)");
            s_xfer_result = ESP_ERR_INVALID_RESPONSE;
            break;
        case USB_TRANSFER_STATUS_TIMED_OUT:
            ESP_LOGW(TAG, "Transfer Timeout");
            s_xfer_result = ESP_ERR_TIMEOUT;
            break;
        case USB_TRANSFER_STATUS_NO_DEVICE:
            ESP_LOGW(TAG, "Transfer NO_DEVICE");
            s_xfer_result = ESP_ERR_INVALID_STATE;
            s_device_connected = false;
            s_device_handle = NULL;
            break;
        default:
            ESP_LOGW(TAG, "Transfer status %d", xfer->status);
            s_xfer_result = ESP_FAIL;
            break;
    }

    if (s_xfer_sem) {
        xSemaphoreGive(s_xfer_sem);
    }
}

// ---------------------------------------------------------------------------
// Device-Callback
// ---------------------------------------------------------------------------
static void device_callback(const usb_host_client_event_msg_t *event_msg,
                            void *arg)
{
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV: {
            ESP_LOGI(TAG, "Neues USB-Gerät an Adresse %d", event_msg->new_dev.address);

            // Device öffnen
            esp_err_t err = usb_host_device_open(s_client_handle,
                                                  event_msg->new_dev.address,
                                                  &s_device_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Device open fehlgeschlagen: %s", esp_err_to_name(err));
                break;
            }

            // Interface 0 claimen (HID-Control)
            err = usb_host_interface_claim(s_client_handle, s_device_handle, 0, 0);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Interface claim fehlgeschlagen: %s", esp_err_to_name(err));
                usb_host_device_close(s_client_handle, s_device_handle);
                s_device_handle = NULL;
                break;
            }

            // Geräte-Info auslesen
            usb_device_info_t dev_info;
            err = usb_host_device_info(s_device_handle, &dev_info);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Device: addr=%d speed=%d MPS=%d",
                         dev_info.dev_addr, dev_info.speed, dev_info.bMaxPacketSize0);
            }

            s_device_connected = true;
            if (s_events) {
                xEventGroupSetBits(s_events, EVENT_DEVICE_CONNECTED);
            }
            ESP_LOGI(TAG, "MVSilicon-Gerät bereit (VID:0x%04X PID:0x%04X)",
                     A800X_USB_VID, A800X_USB_PID);
            break;
        }

        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            ESP_LOGI(TAG, "USB-Gerät getrennt");
            s_device_handle = NULL;
            s_device_connected = false;
            if (s_events) {
                xEventGroupSetBits(s_events, EVENT_DEVICE_DISCONNECTED);
            }
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Client-Task
// ---------------------------------------------------------------------------
static void usb_host_client_task(void *arg)
{
    ESP_LOGI(TAG, "USB-Host-Client-Task gestartet");

    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = device_callback,
            .callback_arg = NULL,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &s_client_handle));

    while (1) {
        usb_host_client_handle_events(s_client_handle, portMAX_DELAY);
    }

    // Wird nie erreicht
    usb_host_client_deregister(s_client_handle);
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Öffentliche API
// ---------------------------------------------------------------------------

esp_err_t usb_host_ctrl_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "USB-Host initialisieren...");

    // Event-Group
    s_events = xEventGroupCreate();
    if (!s_events) {
        return ESP_ERR_NO_MEM;
    }

    // Transfer-Semaphor
    s_xfer_sem = xSemaphoreCreateBinary();
    if (!s_xfer_sem) {
        vEventGroupDelete(s_events);
        s_events = NULL;
        return ESP_ERR_NO_MEM;
    }

    // USB-Host-Treiber installieren
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install fehlgeschlagen: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_xfer_sem);
        s_xfer_sem = NULL;
        vEventGroupDelete(s_events);
        s_events = NULL;
        return err;
    }

    // Client-Task starten (Core 0 für USB)
    xTaskCreatePinnedToCore(
        usb_host_client_task,
        "usb_client",
        A800X_USB_HOST_TASK_STACK_SIZE,
        NULL,
        A800X_USB_HOST_TASK_PRIORITY,
        NULL,
        0
    );

    s_initialized = true;
    ESP_LOGI(TAG, "USB-Host initialisiert");
    return ESP_OK;
}

void usb_host_vbus_enable(bool enable)
{
#if A800X_GPIO_VBUS_ENABLE > 0
    gpio_set_direction(A800X_GPIO_VBUS_ENABLE, GPIO_MODE_OUTPUT);
    gpio_set_level(A800X_GPIO_VBUS_ENABLE,
                   enable ? A800X_VBUS_ENABLE_ACTIVE_HIGH
                          : !A800X_VBUS_ENABLE_ACTIVE_HIGH);
    ESP_LOGI(TAG, "VBUS %s", enable ? "EIN" : "AUS");
#else
    (void)enable;
#endif
}

esp_err_t usb_host_ctrl_wait_for_device(uint16_t vid, uint16_t pid,
                                        uint32_t timeout_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Warte auf Device VID:0x%04X PID:0x%04X (%d ms)...",
             vid, pid, timeout_ms);

    EventBits_t bits = xEventGroupWaitBits(
        s_events, EVENT_DEVICE_CONNECTED,
        pdTRUE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms)
    );

    if (!(bits & EVENT_DEVICE_CONNECTED)) {
        ESP_LOGW(TAG, "Device-Timeout nach %d ms", timeout_ms);
        return ESP_ERR_TIMEOUT;
    }

    if (s_device_handle) {
        ESP_LOGI(TAG, "Device verbunden und konfiguriert");
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

// ---------------------------------------------------------------------------
// HID Control Transfers
// ---------------------------------------------------------------------------

static esp_err_t submit_control_transfer(
    uint8_t bmRequestType, uint8_t bRequest,
    uint16_t wValue, uint16_t wIndex,
    uint16_t wLength,
    const uint8_t *out_data, uint16_t out_len,
    uint8_t *in_buffer, uint16_t in_buf_size,
    uint16_t *in_len)
{
    if (!s_device_connected || !s_device_handle || !s_client_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    // Transfer allozieren: 8 Byte Setup + 256 Byte Daten
    usb_transfer_t *xfer;
    esp_err_t err = usb_host_transfer_alloc(264, 0, &xfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Transfer alloc fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    // Setup-Packet (8 Byte am Anfang des data_buffer)
    usb_setup_packet_t *setup = (usb_setup_packet_t *)xfer->data_buffer;
    setup->bmRequestType = bmRequestType;
    setup->bRequest      = bRequest;
    setup->wValue        = wValue;
    setup->wIndex        = wIndex;
    setup->wLength       = wLength;

    // Daten nach dem Setup-Packet
    if (out_data && out_len > 0) {
        uint16_t copy_len = (out_len < 256) ? out_len : 256;
        memset(xfer->data_buffer + 8, 0, 256);
        memcpy(xfer->data_buffer + 8, out_data, copy_len);
    } else {
        memset(xfer->data_buffer + 8, 0, 256);
    }

    xfer->num_bytes       = 8 + wLength;
    xfer->callback        = transfer_callback;
    xfer->context         = NULL;
    xfer->device_handle   = s_device_handle;
    xfer->bEndpointAddress = 0;  // Control EP 0
    xfer->timeout_ms      = 5000;
    xfer->flags           = 0;

    // Semaphor zurücksetzen
    xSemaphoreTake(s_xfer_sem, 0);
    s_xfer_result = ESP_ERR_TIMEOUT;
    s_xfer_actual = 0;

    // Control Transfer submit
    err = usb_host_transfer_submit_control(s_client_handle, xfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Transfer submit fehlgeschlagen: %s", esp_err_to_name(err));
        usb_host_transfer_free(xfer);
        return err;
    }

    // Auf Completion warten (5 s Timeout)
    if (xSemaphoreTake(s_xfer_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Transfer-Timeout (5 s)");
        usb_host_transfer_free(xfer);
        return ESP_ERR_TIMEOUT;
    }

    // Ergebnis kopieren
    err = s_xfer_result;
    if (err == ESP_OK && in_buffer && in_buf_size > 0 && s_xfer_actual > 8) {
        uint16_t resp_len = s_xfer_actual - 8;  // Setup-Packet abziehen
        if (resp_len > in_buf_size) resp_len = in_buf_size;
        memcpy(in_buffer, xfer->data_buffer + 8, resp_len);
        if (in_len) *in_len = resp_len;
    } else if (in_len) {
        *in_len = 0;
    }

    usb_host_transfer_free(xfer);
    return err;
}

esp_err_t usb_host_ctrl_send_report(const uint8_t *data, uint16_t length)
{
    // HID SET_REPORT: Host-to-Device, Class, Interface
    // bmRequestType: 0x21
    // bRequest: 0x09 (SET_REPORT)
    // wValue: 0x0200 (Report Type = Output, Report ID = 0)
    // wIndex: 0x0000 (Interface 0)
    // wLength: 256

    ESP_LOGD(TAG, "HID SET_REPORT (%d Bytes Nutzdaten)", length);

    return submit_control_transfer(
        0x21,       // bmRequestType: Host-to-Device, Class, Interface
        0x09,       // bRequest: SET_REPORT
        0x0200,     // wValue: Report Type Output, Report ID 0
        0x0000,     // wIndex: Interface 0
        256,        // wLength
        data,       // out_data
        length,     // out_len
        NULL,       // in_buffer
        0,          // in_buf_size
        NULL        // in_len
    );
}

esp_err_t usb_host_ctrl_get_report(uint8_t *buffer, uint16_t *out_length)
{
    // HID GET_REPORT: Device-to-Host, Class, Interface
    // bmRequestType: 0xA1
    // bRequest: 0x01 (GET_REPORT)
    // wValue: 0x0100 (Report Type = Input, Report ID = 0)
    // wIndex: 0x0000 (Interface 0)
    // wLength: 256

    ESP_LOGD(TAG, "HID GET_REPORT");

    return submit_control_transfer(
        0xA1,       // bmRequestType: Device-to-Host, Class, Interface
        0x01,       // bRequest: GET_REPORT
        0x0100,     // wValue: Report Type Input, Report ID 0
        0x0000,     // wIndex: Interface 0
        256,        // wLength
        NULL,       // out_data
        0,          // out_len
        buffer,     // in_buffer
        256,        // in_buf_size
        out_length  // in_len
    );
}

bool usb_host_ctrl_is_device_connected(void)
{
    return s_device_connected;
}

void usb_host_ctrl_deinit(void)
{
    if (!s_initialized) return;

    ESP_LOGI(TAG, "USB-Host deinitialisieren...");

    if (s_device_handle) {
        usb_host_interface_release(s_client_handle, s_device_handle, 0);
        usb_host_device_close(s_client_handle, s_device_handle);
        s_device_handle = NULL;
    }

    s_device_connected = false;

    if (s_xfer_sem) {
        vSemaphoreDelete(s_xfer_sem);
        s_xfer_sem = NULL;
    }

    if (s_events) {
        vEventGroupDelete(s_events);
        s_events = NULL;
    }

    usb_host_uninstall();
    s_initialized = false;
    ESP_LOGI(TAG, "USB-Host deinitialisiert");
}
