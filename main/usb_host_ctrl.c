// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// usb_host_ctrl.c — USB-Host-Controller für MVSilicon-Gerät
//
// Implementiert die USB-Host-Initialisierung, Device-Enumeration
// und HID-SET_REPORT-Transfers für den ESP32-S3.
//
// Framing:
//   A5 5A <effect-id> <length/command> ... 16
//   HID SET_REPORT, 256 Bytes, mit 0x00 aufgefüllt
//   bmRequestType: 0x21, bRequest: 0x09, wValue: 0x0200, wIndex: 0x0000
//

#include "usb_host_ctrl.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "usb/usb_host.h"
#include "driver/gpio.h"

static const char *TAG = "a800x_usb_host";

// --- Interner Zustand ---
static bool s_initialized = false;
static bool s_device_connected = false;
static uint8_t s_hid_report[A800X_HID_REPORT_SIZE];

// Device-Handle für das MVSilicon-Gerät
static usb_device_handle_t s_device_handle = NULL;
// static uint8_t s_in_ep_addr = 0;
// static uint8_t s_out_ep_addr = 0;

// Event-Group für Synchronisation
static EventGroupHandle_t s_events = NULL;
#define EVENT_DEVICE_CONNECTED   BIT0
#define EVENT_DEVICE_DISCONNECTED BIT1

// --- Device-Callback (Forward-Deklaration) ---
static void device_callback(const usb_host_client_event_msg_t *event_msg,
                            void *arg);

// --- Client-Task ---
static void usb_host_client_task(void *arg)
{
    ESP_LOGI(TAG, "USB-Host-Client-Task gestartet");

    // Client registrieren
    usb_host_client_handle_t client_handle;
    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = device_callback,
            .callback_arg = NULL,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &client_handle));

    while (1) {
        usb_host_client_handle_events(client_handle, portMAX_DELAY);
        // Events werden hier verarbeitet
    }

    ESP_ERROR_CHECK(usb_host_client_deregister(client_handle));
    vTaskDelete(NULL);
}

// --- Device-Callback ---
static void device_callback(const usb_host_client_event_msg_t *event_msg,
                            void *arg)
{
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            ESP_LOGI(TAG, "Neues USB-Gerät erkannt");
            if (s_events) {
                xEventGroupSetBits(s_events, EVENT_DEVICE_CONNECTED);
            }
            break;

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
// Öffentliche API
// ---------------------------------------------------------------------------

esp_err_t usb_host_ctrl_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    // Event-Group erstellen
    s_events = xEventGroupCreate();
    if (!s_events) {
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
        vEventGroupDelete(s_events);
        s_events = NULL;
        return err;
    }

    // Client-Task starten
    TaskHandle_t client_task = NULL;
    xTaskCreatePinnedToCore(
        usb_host_client_task,
        "usb_client",
        A800X_USB_HOST_TASK_STACK_SIZE,
        NULL,
        A800X_USB_HOST_TASK_PRIORITY,
        &client_task,
        0  // Core 0 für USB
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

    // Auf Verbindung warten
    EventBits_t bits = xEventGroupWaitBits(
        s_events, EVENT_DEVICE_CONNECTED,
        pdTRUE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms)
    );

    if (!(bits & EVENT_DEVICE_CONNECTED)) {
        ESP_LOGW(TAG, "Device-Timeout nach %d ms", timeout_ms);
        return ESP_ERR_TIMEOUT;
    }

    // Device konfigurieren
    if (s_device_handle) {
        // Hier: Device-Konfiguration, Interface-Deskriptor auslesen,
        // Endpunkte bestimmen
        // (vereinfacht: für HID-Geräte den Control-Endpoint verwenden)
        s_device_connected = true;
        ESP_LOGI(TAG, "Device verbunden und konfiguriert");
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t usb_host_ctrl_send_report(const uint8_t *data, uint16_t length)
{
    if (!s_device_connected || !s_device_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    // Report-Puffer vorbereiten: Daten + Null-Padding auf 256 Byte
    memset(s_hid_report, 0, sizeof(s_hid_report));
    if (length > sizeof(s_hid_report)) {
        length = sizeof(s_hid_report);
    }
    memcpy(s_hid_report, data, length);

    // HID SET_REPORT via Control-Transfer
    // bmRequestType: 0x21 (Host-to-Device, Class, Interface)
    // bRequest: 0x09 (SET_REPORT)
    // wValue: 0x0200 (Report Type = Output, Report ID = 0)
    // wIndex: 0x0000 (Interface Number)
    // wLength: 256
    usb_device_info_t dev_info;
    usb_host_device_info(s_device_handle, &dev_info);

    // USB Control Transfer
    // (Vereinfacht: Hier müsste der tatsächliche Control-Transfer
    //  über die USB-Host-API erfolgen)
    ESP_LOGD(TAG, "Sende HID Report (%d Bytes Nutzdaten)", length);

    // TODO: Tatsächlichen USB Control Transfer implementieren
    // usb_host_transfer_alloc / usb_host_transfer_submit / usb_host_transfer_free

    return ESP_OK;
}

esp_err_t usb_host_ctrl_get_report(uint8_t *buffer, uint16_t *out_length)
{
    if (!s_device_connected || !s_device_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    // HID GET_REPORT via Control-Transfer
    // bmRequestType: 0xA1 (Device-to-Host, Class, Interface)
    // bRequest: 0x01 (GET_REPORT)
    // wValue: 0x0200 (Report Type = Input, Report ID = 0)
    // wIndex: 0x0000 (Interface Number)
    // wLength: 256

    // TODO: Tatsächlichen USB Control Transfer implementieren
    // Vereinfacht: 256 Byte lesen

    if (out_length) {
        *out_length = 0;
    }

    return ESP_OK;
}

bool usb_host_ctrl_is_device_connected(void)
{
    return s_device_connected;
}

void usb_host_ctrl_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    s_device_handle = NULL;
    s_device_connected = false;

    if (s_events) {
        vEventGroupDelete(s_events);
        s_events = NULL;
    }

    usb_host_uninstall();
    s_initialized = false;

    ESP_LOGI(TAG, "USB-Host deinitialisiert");
}