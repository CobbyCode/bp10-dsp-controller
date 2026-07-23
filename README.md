# BP10 DSP Controller

BP10 DSP Controller is an independent ESP32-S3 USB host controller for selected MVSilicon BP10/BP1048 DSP devices.

It provides a local web interface for controlling the DSP functions commonly found on supported subwoofer boards. The controller communicates with the DSP through its USB ACP/HID interface, stores the selected configuration on the ESP32, and restores it after power-on or a DSP reconnect.

The project was originally developed for the **AIYIMA A800X** and now also includes experimental support for other compatible MVSilicon ACP devices.

## Features

Depending on the connected DSP and the functions discovered on it, the web interface provides:

* Music Pre EQ
* Music Noise Suppressor
* Virtual Bass
* Music DRC
* Silence Detector, when available
* graphical Pre EQ frequency-response preview
* automatic device and effect discovery for supported Generic ACP devices
* automatic restoration of saved DSP settings
* DSP configuration export and import
* explicit reload of the current state from the DSP
* Wi-Fi setup with network scanning
* configurable device name
* factory reset
* firmware updates through the web interface

Only functions recognized on the connected device are displayed.

## Configuration storage

DSP settings are stored in the ESP32's non-volatile storage and are reapplied after:

* restarting the ESP32
* powering the DSP off and on
* disconnecting and reconnecting the DSP USB connection

The controller does **not** write configuration data to the DSP's internal flash.

For Generic ACP devices, saved configurations are tied to the detected device profile and DSP schema. A stored configuration is restored only when the connected device matches the profile for which it was saved.

Configuration export and import contain DSP settings only. They do not include Wi-Fi passwords, network information, IP addresses, MAC addresses, or the ESP32 device name.

## Supported DSP devices

### AIYIMA A800X

* USB ID: `0x8888:0x171E`
* fixed and fully tested device profile
* Music Pre EQ
* Music Noise Suppressor
* Virtual Bass
* Music DRC
* Silence Detector
* configuration persistence
* automatic restore
* configuration export and import

The AIYIMA A800X is currently the primary verified target.

### Generic MVSilicon ACP devices

* currently recognized USB ID: `0x8888:0x1719`
* experimental automatic ACP effect discovery
* dynamic effect addresses instead of an A800X-specific fixed map
* only recognized and structurally compatible functions are enabled
* device-profile-bound configuration persistence
* automatic restore on a matching device
* configuration export and import

Generic ACP support has been tested with a BP1048-based board exposing the supported Music Pre EQ, Noise Suppressor, Virtual Bass and DRC schemas.

Other BP10/BP1048 devices may use different firmware, effect layouts or parameter structures. Detection of a USB device alone therefore does not guarantee compatibility.

## ESP32-S3 hardware

The canonical firmware target is an ESP32-S3 board with:

* 4 MB flash
* 2 MB Quad PSRAM
* USB host capability
* Wi-Fi

The release firmware and partition layout are built for this configuration.

Development boards with more flash or PSRAM may also be used for development, but the published product firmware targets the 4 MB flash / 2 MB PSRAM hardware.

![BP10 controller hardware installation](docs/images/bp10-hardware-installation.png)

## Web interface

![BP10 DSP Controller dashboard](docs/images/bp10-dashboard-overview.png)

### Pre EQ response preview

The Pre EQ editor displays the combined frequency response of the currently configured filters.

![BP10 Pre EQ frequency response](docs/images/bp10-preeq-response.png)

## Installing the prebuilt firmware

The easiest first installation method is Espressif's browser-based serial flasher.

1. Download `bp10-dsp-controller-full.bin` from the latest GitHub release.
2. Open the Espressif Web Flasher in Chrome or Edge:
 `https://espressif.github.io/esptool-js/`
3. Connect the ESP32-S3 to the computer using a USB data cable.
4. Select the ESP32-S3 serial port.
5. Add `bp10-dsp-controller-full.bin` at flash address **`0x0`**.
6. Start programming.
7. Restart the ESP32-S3 after flashing.

When the board is not detected, hold its **BOOT** button while connecting or resetting it to enter download mode.

Do not use the smaller `bp10_dsp_controller.bin` application image for the first installation. That file is intended only for firmware updates on an already installed controller.

## First Wi-Fi setup

1. Connect to the open Wi-Fi network named `bp10-xxxx`.
2. Open `http://192.168.4.1`.
3. Scan for available networks.
4. Select the home network and enter its password.
5. After the ESP32 connects, open the displayed IP address or:
 `http://bp10-xxxx.local`

The setup access point is disabled after a successful home-network connection.

It becomes available again when:

* no Wi-Fi configuration has been saved
* the saved network cannot be reached
* a factory reset has been performed

## Firmware updates

After the initial installation, firmware can be updated directly from the web interface.

Use the application-only image:

```text
bp10_dsp_controller.bin
```

The controller validates the firmware before activating it. Wi-Fi settings and saved DSP profiles remain stored during a normal firmware update.

The full image is intended for initial serial flashing or complete recovery.

## Building from source

The tested toolchain is:

```text
ESP-IDF 6.0.2
```

From a fresh checkout:

```bash
idf.py set-target esp32s3
idf.py build
idf.py merge-bin -o bp10-dsp-controller-full.bin
```

The resulting application-only firmware is:

```text
build/bp10_dsp_controller.bin
```

The merged image for first installation is:

```text
build/bp10-dsp-controller-full.bin
```

Run the host regression tests with:

```bash
tests/host/run.sh
```

The canonical configuration in the repository targets 4 MB flash and 2 MB Quad PSRAM. When switching between development-board and product configurations, use separate build directories or regenerate the configuration from the appropriate defaults.

## Network security

The web interface is intended for use on a trusted local network.

It currently has no user login or authentication. Any device on the same network may be able to access the DSP control, configuration and firmware-update endpoints.

Do not expose the controller directly to the internet.

## Safety notes

* Settings are changed on the live DSP.
* The project does not write settings to the DSP's internal flash.
* Import files are validated before they are applied.
* Generic ACP support remains experimental.
* Keep a known working configuration backup before testing unfamiliar hardware.
* Use the firmware at your own risk.

## License

Copyright 2026 CobbyCode.

This project is licensed under the GNU General Public License v3.0 or later. See [LICENSE](LICENSE).

## Disclaimer

This is an independent open-source project.

It is not affiliated with, endorsed by, or supported by MVSilicon, AIYIMA, or any other hardware manufacturer. Product and company names are used only to identify compatible or tested hardware.
