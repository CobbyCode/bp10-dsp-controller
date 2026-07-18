# AIYIMA BP10 DSP Controller

Ein eigenständiger ESP32-S3-USB-Host-Controller für den MVSilicon-DSP im AIYIMA A800X.

## Zweck

Der ESP32-S3 wird dauerhaft im AIYIMA A800X eingebaut. Nach jedem Einschalten stellt er
die DSP-Parameter wieder her (z. B. Noise Suppressor aus, Virtual Bass aus, PreEQ, DRC).

Er bietet eine lokale Weboberfläche zur Konfiguration.

## Hardware

- **Ziel:** ESP32-S3 mit nativer USB-Host-Fähigkeit
- **USB-Gerät:** MVSilicon BP10 / AIYIMA A800X — VID `0x8888`, PID `0x171E`
- **Transport:** USB HID SET_REPORT, 256 Bytes
- **Flash:** 16 MB
- **PSRAM:** Octal SPI PSRAM (ESP32-S3)

## ESP-IDF-Version

**v6.0.2** (getaggt, von https://github.com/espressif/esp-idf)

Installation:

```bash
mkdir -p ~/esp
cd ~/esp
git clone --depth 1 --branch v6.0.2 --shallow-submodules --recursive \
    https://github.com/espressif/esp-idf.git esp-idf-v6.0.2
cd esp-idf-v6.0.2
./install.sh esp32s3
```

Umgebungsvariablen (in `~/.bashrc` oder je Session):

```bash
alias get_idf='. ~/esp/esp-idf-v6.0.2/export.sh'
```

## Projektstruktur

```
bp10-dsp-controller/
├── CMakeLists.txt             # Top-Level CMake
├── sdkconfig.defaults         # SDK-Vorgaben für ESP32-S3
├── partitions.csv             # Partitionstabelle (16 MB)
├── .gitignore
├── README.md
├── main/
│   ├── CMakeLists.txt         # Main-Komponente
│   ├── Kconfig.projbuild      # Projekt-Konfiguration
│   ├── main.c                 # Einstiegspunkt, Init-Reihenfolge
│   ├── app_config.h           # Zentrale Board-Konfiguration
│   │
│   ├── usb_host_ctrl.c/.h     # USB-Host-Initialisierung, Device-Enumeration
│   ├── mvs_protocol.c/.h      # MVSilicon-Protokoll (Encoder/Decoder)
│   ├── dsp_model.c/.h         # DSP-Modell (Zustand, Parameter)
│   ├── nvs_settings.c/.h      # NVS-Einstellungen (WiFi, Profile)
│   ├── wifi_manager.c/.h      # WLAN-Management, SoftAP, Captive Portal
│   ├── mdns_service.c/.h      # mDNS-Dienst
│   ├── http_server.c/.h       # HTTP-Server
│   ├── api_handlers.c/.h      # REST-API-Endpunkte
│   ├── web_ui.c/.h            # Eingebettete Web-UI-Assets
│   ├── ota_update.c/.h        # OTA-Firmwareupdate
│   ├── config_io.c/.h         # Konfigurations-Import/Export
│   │
│   ├── www/                   # Web-UI (HTML, CSS, JS)
│   │   ├── index.html
│   │   ├── style.css
│   │   └── app.js
│   │
│   └── test/                  # Unit-Tests
│       ├── CMakeLists.txt
│       ├── test_main.c
│       ├── mock_usb_transport.c/.h
│       ├── test_protocol.c
│       └── test_dsp_model.c
```

## Module

| Modul | Beschreibung |
|---|---|
| `usb_host_ctrl` | USB-Host-Initialisierung, Enumeration des MVSilicon-Geräts, HID-Transfer |
| `mvs_protocol` | MVSilicon-Protokoll-Encoder/Decoder: Framing, Effekt-IDs, Readback-Parsing |
| `dsp_model` | DSP-Zustandsmodell: Noise Suppressor, Virtual Bass, PreEQ, DRC, Silence Detector |
| `nvs_settings` | Nichtflüchtige Speicherung: WiFi-Zugangsdaten, DSP-Profile, Gerätename |
| `wifi_manager` | WLAN: SoftAP + Captive Portal, Heim-WLAN-Verbindung, Provisioning |
| `mdns_service` | mDNS-Anmeldung: `bp10-xxxx.local` |
| `http_server` | HTTP-Server (ESP HTTP Server), statische Dateien, REST-API-Routing |
| `api_handlers` | REST-API-Endpunkte: DSP-Status, Parameter setzen, Profile, OTA, Config |
| `web_ui` | Eingebettete Web-UI-Assets (HTML, CSS, JS) |
| `ota_update` | OTA-Firmwareupdate via HTTP/HTTPS |
| `config_io` | Export/Import der gesamten Konfiguration als JSON |
| `test/` | Unit-Tests mit Mock-USB-Transport |

## Build

```bash
cd /path/to/bp10-dsp-controller
source ~/esp/esp-idf-v6.0.2/export.sh
idf.py set-target esp32s3
idf.py build
```

## Test

```bash
tests/host/run.sh
```

The host regression suite exercises ACP catalog parsing, exact name/type
mapping, USB transport/setup selection, schema-aware PreEQ handling, Classic
DRC decoding and array writes, and A800X wire-frame compatibility. The legacy
ESP-IDF Unity sources under `main/test/` remain available for target-side test
firmware but are not part of the normal application build.

## BIN-Pfade (nach Build)

```
build/bp10_dsp_controller.bin       # Bootloader + App
build/ota_data_initial.bin           # OTA-Daten-Initialisierung
build/partitions.bin                 # Partitionstabelle
build/bootloader/bootloader.bin      # Bootloader
```

## Netzwerkmodell

1. **Erstes Setup:** SoftAP (`bp10-xxxx`) + Captive Portal → Heim-WLAN eingeben
2. **Normalbetrieb:** Verbindung zum Heim-WLAN, mDNS: `bp10-xxxx.local`
3. **Name änderbar:** z. B. `bp10-left` / `bp10-right`
4. **IP:** wird in der Oberfläche angezeigt
5. **Nach Konfiguration:** WLAN kann deaktiviert werden (Konfiguration nur nach Power-Cycle)

## Wichtige Hinweise

- **Kein DSP-Flash-Save (`0xFD`) beim Booten** — der BP10 speichert Parameter nicht dauerhaft
- **PEQ:** Vollständigen PreEQ-Zustand lesen, nur gewünschte Felder ändern, vollständig zurückschreiben
- **USB-Host:** ESP32-S3 native USB-Host-Fähigkeit (kein externer USB-Host-Controller nötig)

## Lizenz

MIT
