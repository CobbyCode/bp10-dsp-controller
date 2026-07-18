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

#define WIFI_MANAGER_SCAN_MAX_RESULTS 5

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} wifi_manager_scan_result_t;

typedef enum {
    WIFI_MANAGER_SCAN_IDLE = 0,
    WIFI_MANAGER_SCAN_RUNNING,
    WIFI_MANAGER_SCAN_DONE,
    WIFI_MANAGER_SCAN_FAILED,
} wifi_manager_scan_state_t;

/** Lifecycle state of the WiFi manager. */
typedef enum {
    WIFI_LIFECYCLE_BOOTING = 0,
    WIFI_LIFECYCLE_AP_ONLY,
    WIFI_LIFECYCLE_AP_STA_CONNECTING,
    WIFI_LIFECYCLE_CONNECTED_AP_TRANSITION,
    WIFI_LIFECYCLE_CONNECTED_STA_ONLY,
    WIFI_LIFECYCLE_DISCONNECTED_RETRYING,
    WIFI_LIFECYCLE_FALLBACK_AP,
} wifi_lifecycle_state_t;

/**
 * @brief WiFi-Manager initialisieren.
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief SoftAP starten (für erstes Setup / Captive Portal).
 *
 * @param hostname AP-Hostname (z. B. "bp10-xxxx")
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
 * @brief Prüfen, ob der SoftAP aktuell aktiv ist.
 */
bool wifi_manager_is_softap_active(void);

/**
 * @brief Aktuelle IP-Adresse als String abrufen.
 *
 * @param ip Ausgabepuffer (mindestens 16 Byte)
 * @param max_len Puffergröße
 * @return esp_err_t
 */
esp_err_t wifi_manager_get_ip_str(char *ip, size_t max_len);

/**
 * @brief Verbundene STA-SSID abrufen.
 *
 * @return esp_err_t ESP_OK wenn verbunden, sonst Fehler
 */
esp_err_t wifi_manager_get_sta_ssid(char *ssid, size_t max_len);

/**
 * @brief Geräte-Hostname abrufen (z. B. "bp10-xxxx").
 */
esp_err_t wifi_manager_get_hostname(char *hostname, size_t max_len);

/**
 * @brief Verbleibende Sekunden bis zur AP-Abschaltung (0 wenn kein Timer läuft).
 */
int wifi_manager_get_ap_shutdown_remaining_sec(void);

/**
 * @brief Aktuellen Lifecycle-State abrufen.
 */
wifi_lifecycle_state_t wifi_manager_get_lifecycle_state(void);

/**
 * @brief Lifecycle-State als String.
 */
const char *wifi_manager_lifecycle_state_str(void);

/**
 * @brief Prüft ob gespeicherte WLAN-Zugangsdaten vorhanden sind.
 */
bool wifi_manager_has_credentials(void);

/**
 * @brief Setzt den Credential-Status (wird von main nach NVS-Lesen gesetzt).
 */
void wifi_manager_set_has_credentials(bool has);

/** Start a user-requested asynchronous scan without stopping AP/STA mode. */
esp_err_t wifi_manager_start_scan(void);

/** Return a snapshot of scan state/results. */
wifi_manager_scan_state_t wifi_manager_get_scan_results(
    wifi_manager_scan_result_t *results, size_t capacity, size_t *count,
    esp_err_t *scan_error);

/** Safe, credential-free connection status for the Web UI. */
void wifi_manager_get_connection_status(char *state, size_t state_len,
                                        char *message, size_t message_len);

/** WLAN-Auto-Off konfigurieren. Deaktiviert bedeutet dauerhaft erreichbar. */
void wifi_manager_configure_auto_off(bool enabled, uint32_t initial_timeout_s,
                                     bool credentials_available);

/** Echte Web-Bedienung registrieren; Status-Polling ruft dies nicht auf. */
void wifi_manager_note_user_activity(void);

bool wifi_manager_auto_off_enabled(void);

/**
 * @brief Hostnamen aus MAC-Adresse generieren.
 *
 * z. B. "bp10-xxxx" aus MAC "7C:DF:A1:E3:3F:21"
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
