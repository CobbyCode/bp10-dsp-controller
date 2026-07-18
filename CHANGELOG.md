# Changelog

## 0.3.7 - 2026-07-18

- Apply complete stored Noise Suppressor, Virtual Bass, PreEQ, and DRC states
  when restoring a DSP profile, while continuing independent module writes and
  returning the first error.
- Preserve safe disabled-state handling for Noise Suppressor and Virtual Bass so
  the DSP receives only the supported disable command without parameter writes.
- Update Noise Suppressor and Virtual Bass decoder length validation to their
  exact payload sizes and add boundary coverage for truncated payloads.
- Keep the cached current profile unchanged when any profile write fails.
- Correct the extended-readback command comment and remove the obsolete profile
  name test.

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
