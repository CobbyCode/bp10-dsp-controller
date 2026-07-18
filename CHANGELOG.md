# Changelog

## 0.3.6 - 2026-07-18

- Correct Virtual Bass enable/disable command ordering and readback validation.
- Restore the BP10 factory defaults for Virtual Bass, Bass Enhanced, and the
  Silence Detector in the model and web UI.
- Replace the retained APSTA configuration with an explicit STA-only transition
  after the home-network address is acquired.
- Stop setup DHCP and captive services when provisioning completes while keeping
  the home-network web interface available.
- Use the consistent `bp10-xxxx` setup SSID and restore it after Factory Reset or
  a persistent STA connection timeout.
- Preserve the recovery deadline across reconnect attempts so fallback setup can
  activate reliably.
- Defer the STA connection until after the provisioning HTTP acknowledgement is
  delivered, preventing the misleading `Failed to fetch` result.
