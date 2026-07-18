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
#include "esp_log_buffer.h"
#include "esp_err.h"
#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"
#include "driver/gpio.h"

static const char *TAG = "bp10_usb_host";

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
            ESP_LOGW(TAG, "Transfer NO_DEVICE – Gerät während Transfers entfernt");
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

            // Device-Descriptor lesen (VID/PID)
            const usb_device_desc_t *dev_desc = NULL;
            err = usb_host_get_device_descriptor(s_device_handle, &dev_desc);
            if (err == ESP_OK && dev_desc) {
                ESP_LOGI(TAG, "USB-EVENT: NEUES DEVICE an Adresse %d",
                         event_msg->new_dev.address);
                ESP_LOGI(TAG, "  VID:0x%04X  PID:0x%04X  bcdUSB:0x%04X  Klasse:%d",
                         dev_desc->idVendor, dev_desc->idProduct,
                         dev_desc->bcdUSB, dev_desc->bDeviceClass);

                if (dev_desc->idVendor == BP10_USB_VID &&
                    dev_desc->idProduct == BP10_USB_PID) {
                    ESP_LOGI(TAG, "✅ BP10 DSP ERKANNT (VID:0x%04X PID:0x%04X)",
                             BP10_USB_VID, BP10_USB_PID);
                } else {
                    ESP_LOGW(TAG, "⚠️ Fremdgerät: VID:0x%04X PID:0x%04X (warte auf BP10 0x%04X:0x%04X)",
                             dev_desc->idVendor, dev_desc->idProduct,
                             BP10_USB_VID, BP10_USB_PID);
                }
            } else {
                ESP_LOGW(TAG, "USB-EVENT: Device an Adresse %d – Descriptor nicht lesbar",
                         event_msg->new_dev.address);
            }

            // Geräte-Info (Speed, MaxPacket)
            usb_device_info_t dev_info;
            err = usb_host_device_info(s_device_handle, &dev_info);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "  Speed:%s  MPS:%d  Config:%d",
                         dev_info.speed == USB_SPEED_LOW ? "Low" :
                         dev_info.speed == USB_SPEED_FULL ? "Full" :
                         dev_info.speed == USB_SPEED_HIGH ? "High" : "?",
                         dev_info.bMaxPacketSize0, dev_info.bConfigurationValue);
            }

            // Config-Descriptor dump (HID-Report-Deskriptor)
            const usb_config_desc_t *config_desc = NULL;
            err = usb_host_get_active_config_descriptor(s_device_handle, &config_desc);
            if (err == ESP_OK && config_desc) {
                ESP_LOGI(TAG, "  Config: %u Interface(s), wTotalLength=%u Bytes",
                         config_desc->bNumInterfaces, config_desc->wTotalLength);
                uint16_t dump_len = config_desc->wTotalLength < 128 ? config_desc->wTotalLength : 128;
                ESP_LOGI(TAG, "  ConfigDesc (%d bytes):", dump_len);
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, config_desc, dump_len, ESP_LOG_INFO);
            } else {
                ESP_LOGW(TAG, "  Config-Descriptor nicht lesbar: %s", esp_err_to_name(err));
            }

            s_device_connected = true;
            ESP_LOGI(TAG, "USB-Gerät sauber enumeriert");
            if (s_events) {
                xEventGroupSetBits(s_events, EVENT_DEVICE_CONNECTED);
            }
            break;
        }

        case USB_HOST_CLIENT_EVENT_DEV_GONE: {
            ESP_LOGW(TAG, "BP10 getrennt – USB-Device-Handle wird freigegeben");

            usb_device_handle_t old_handle = s_device_handle;

            // Handle sauber freigeben, sonst blockiert der alte Handle
            // die Enumeration eines neuen Geräts.
            if (s_device_handle) {
                esp_err_t release_err = usb_host_interface_release(
                    s_client_handle, s_device_handle, 0);
                if (release_err != ESP_OK) {
                    ESP_LOGE(TAG, "interface_release fehlgeschlagen: %s",
                             esp_err_to_name(release_err));
                }

                esp_err_t close_err = usb_host_device_close(
                    s_client_handle, s_device_handle);
                if (close_err != ESP_OK) {
                    ESP_LOGE(TAG, "device_close fehlgeschlagen: %s",
                             esp_err_to_name(close_err));
                }

                ESP_LOGD(TAG, "Handle %p freigegeben", (void *)old_handle);
            } else {
                ESP_LOGD(TAG, "DEV_GONE: Handle bereits NULL (Double-Event)");
            }

            s_device_handle = NULL;
            s_device_connected = false;

            if (s_events) {
                xEventGroupSetBits(s_events, EVENT_DEVICE_DISCONNECTED);
            }
            break;
        }

        case USB_HOST_CLIENT_EVENT_DEV_SUSPENDED:
            ESP_LOGI(TAG, "USB-EVENT: DEVICE SUSPENDED");
            break;

        case USB_HOST_CLIENT_EVENT_DEV_RESUMED:
            ESP_LOGI(TAG, "USB-EVENT: DEVICE RESUMED");
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
// Library-Event-Task (verarbeitet Bus-Events wie Device-Erkennung)
// ---------------------------------------------------------------------------
static void usb_host_lib_task(void *arg)
{
    ESP_LOGI(TAG, "USB-Host-Library-Task gestartet (verarbeitet Bus-Events)");

    while (1) {
        uint32_t event_flags = 0;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Library-Event-Fehler: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "LIB-EVENT: NO_CLIENTS");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "LIB-EVENT: ALL_FREE");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_AUTO_SUSPEND) {
            ESP_LOGD(TAG, "LIB-EVENT: AUTO_SUSPEND");
        }
    }

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
        .peripheral_map = BIT0,
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

    ESP_LOGI(TAG, "USB-Host-Treiber installiert (peripheral_map=0x%x)", BIT0);

    // Library-Event-Task starten (MUSS Bus-Events verarbeiten!)
    xTaskCreatePinnedToCore(
        usb_host_lib_task,
        "usb_lib",
        BP10_USB_HOST_TASK_STACK_SIZE,
        NULL,
        BP10_USB_HOST_TASK_PRIORITY,
        NULL,
        0
    );

    // Client-Task starten (Core 0 für USB)
    xTaskCreatePinnedToCore(
        usb_host_client_task,
        "usb_client",
        BP10_USB_HOST_TASK_STACK_SIZE,
        NULL,
        BP10_USB_HOST_TASK_PRIORITY,
        NULL,
        0
    );

    s_initialized = true;
    ESP_LOGI(TAG, "USB-Host initialisiert");
    return ESP_OK;
}

void usb_host_vbus_enable(bool enable)
{
#if BP10_GPIO_VBUS_ENABLE > 0
    gpio_set_direction(BP10_GPIO_VBUS_ENABLE, GPIO_MODE_OUTPUT);
    gpio_set_level(BP10_GPIO_VBUS_ENABLE,
                   enable ? BP10_VBUS_ENABLE_ACTIVE_HIGH
                          : !BP10_VBUS_ENABLE_ACTIVE_HIGH);
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
        ESP_LOGD(TAG, "Control-Transfer abgelehnt: connected=%d",
                 s_device_connected);
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
        ESP_LOGD(TAG, "TX Hex: %02x %02x %02x %02x %02x %02x %02x %02x...",
                 out_data[0], out_data[1], out_data[2], out_data[3],
                 out_data[4], out_data[5], out_data[6], out_data[7]);
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
        ESP_LOGD(TAG, "RX aktual=%d resp=%d Hex: %02x %02x %02x %02x %02x %02x %02x %02x...",
                 s_xfer_actual, resp_len,
                 xfer->data_buffer[8], xfer->data_buffer[9],
                 xfer->data_buffer[10], xfer->data_buffer[11],
                 xfer->data_buffer[12], xfer->data_buffer[13],
                 xfer->data_buffer[14], xfer->data_buffer[15]);
    } else if (in_len) {
        *in_len = 0;
    }

    usb_host_transfer_free(xfer);
    return err;
}

esp_err_t usb_host_ctrl_send_report(const uint8_t *data, uint16_t length)
{
    // HID SET_REPORT: Host-to-Device, Class, Interface
    // Output Report (0x0200), Report ID 0.

    ESP_LOGD(TAG, "HID SET_REPORT Output Report (%d Bytes Nutzdaten)", length);

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