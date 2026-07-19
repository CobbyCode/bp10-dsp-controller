# SPDX-FileCopyrightText: 2026 CobbyCode
# SPDX-License-Identifier: GPL-3.0-or-later
#
# CMakeLists.txt — main component
#

idf_component_register(
    SRCS
        "main.c"
        "usb_host_ctrl.c"
        "mvs_usb_profile.c"
        "mvs_protocol.c"
        "mvs_device_profile.c"
        "mvs_device_runtime.c"
        "dsp_model.c"
        "nvs_settings.c"
        "wifi_manager.c"
        "mdns_service.c"
        "http_server.c"
        "api_handlers.c"
        "web_ui.c"
        "ota_update.c"
        "config_io.c"
    INCLUDE_DIRS
        "."
        "test/mock"
    EMBED_FILES
        "www/index.html"
        "www/style.css"
        "www/app.js"
    REQUIRES
        nvs_flash
        esp_wifi
        mdns
        esp_http_server
        esp-tls
        esp_https_ota
        usb
        driver
        app_update
        esp_driver_gpio
        esp_timer
)
