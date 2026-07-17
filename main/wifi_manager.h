// SPDX-FileCopyrightText: 2026 PaulsKlaue
// SPDX-License-Identifier: MIT
//
// wifi_manager.h — WLAN-Management, SoftAP, Captive Portal, Provisioning
//

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// WiFi-Ereignis-Callbacks
typedef void (*wifi_connected_cb_t)(void);
typedef void (*wifi_disconnected_cb_t)(void);

/**
 * @brief WiFi-Manager initialisieren.
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief SoftAP starten (für erstes Setup / Captive Portal).
 *
 * @param hostname AP-Hostname (z. B. "a800x-3f21")
 * @return esp_err_t
 */
esp_err_t wifi_manager_start_softap(const char *hostname);

/**
 * @brief SoftAP stoppen.
 */
esp_err_t wifi_manager_stop_softap(void);

/**
 * @brief Als Station mit Heim-WLAN verbinden.
 *
 * @param ssid SSID
 * @param password Passwort
 * @return esp_err_t
 */
esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password);

/**
 * @brief Station trennen.
 */
esp_err_t wifi_manager_disconnect_sta(void);

/**
 * @brief Prüfen, ob eine Station-WiFi-Verbindung besteht.
 *
 * @return true wenn verbunden
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Aktuelle IP-Adresse als String abrufen.
 *
 * @param ip Ausgabepuffer (mindestens 16 Byte)
 * @param max_len Puffergröße
 * @return esp_err_t
 */
esp_err_t wifi_manager_get_ip_str(char *ip, size_t max_len);

/**
 * @brief Hostnamen aus MAC-Adresse generieren.
 *
 * z. B. "a800x-3f21" aus MAC "7C:DF:A1:E3:3F:21"
 *
 * @param hostname Ausgabepuffer
 * @param max_len Puffergröße
 */
void wifi_manager_generate_hostname(char *hostname, size_t max_len);

/**
 * @brief MAC-Adresse als String abrufen.
 *
 * @param mac Ausgabepuffer (mindestens 18 Byte)
 */
void wifi_manager_get_mac_str(char *mac);

/**
 * @brief Captive Portal starten (DNS-Weiterleitung + HTTP-Redirect).
 *
 * Leitet alle DNS-Anfragen während des SoftAP-Modus auf den ESP um.
 *
 * @return esp_err_t
 */
esp_err_t wifi_manager_start_captive_portal(void);

/**
 * @brief Captive Portal stoppen.
 */
esp_err_t wifi_manager_stop_captive_portal(void);

/**
 * @brief Verbindungs-Callback registrieren.
 */
void wifi_manager_on_connected(wifi_connected_cb_t cb);

/**
 * @brief Trennung-Callback registrieren.
 */
void wifi_manager_on_disconnected(wifi_disconnected_cb_t cb);

/**
 * @brief WiFi-Manager deinitialisieren.
 */
void wifi_manager_deinit(void);

#ifdef __cplusplus
}
#endif