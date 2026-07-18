# TASKS

## Goal

SoftAP lifecycle must expose setup AP only when provisioning or recovery is required.

## Current step

- [x] Diagnose `ESP_014019`, implement deterministic APSTA/STA transitions, and verify on hardware.
- [x] Confirm the complete provisioning flow on a phone and close the release.

## Expected result

- Factory reset exposes `bp10-xxxx` and permits setup.
- Successful STA DHCP stops setup services and switches to `WIFI_MODE_STA`.
- Reboot with valid credentials has no visible SoftAP.
- Persistent STA failure restarts recovery SoftAP after the configured timeout.

## Release

- `0.3.6`: accepted and prepared as a local tagged release on 2026-07-18.
