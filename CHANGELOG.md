# Changelog

## 0.4.2 - 2026-07-22

- Add one versioned JSON configuration format for the AIYIMA A800X and
  matching Generic ACP devices.
- Bind Generic ACP imports to the discovered schema fingerprint before any
  DSP or NVS write, and reject missing or unsupported format versions.
- Route imported Generic profiles through the shared DSP apply, full readback
  verification, runtime commit, and fingerprint-bound NVS persistence path.
- Enable configuration export and import in the web UI for Generic ACP
  devices with a valid fingerprint while keeping factory values A800X-only.

## 0.4.1 - 2026-07-21

- Enable Generic-Persistence (fingerprint-bound ESP-NVS) for non-A800X
  profiles so DSP state is restored after power cycles.
- Fix Generic factory defaults isolation: a Factory Reset on Generic
  hardware clears controller-side persistence and runs read-only DSP
  discovery instead of writing stale A800X default values.
- Add Virtual Bass Classic (type 23) support for the Generic Classic
  profile with correct 6-byte wire encoding.
- Add capability-gated Music Phase and Music Delay/HQ controls with
  immediate readback, shared apply-with-readback helper, and UI cards
  styled to match existing DSP control cards.

## 0.4.0 - 2026-07-19

- Add capability-gated Generic Virtual Bass Classic, Music Phase, and Music
  Delay/HQ controls with immediate readback, simple UI cards, and backward-safe
  fingerprint-bound ESP-NVS persistence; fixed A800X mappings remain unchanged.
- Scope factory defaults to the fixed A800X profile. Generic Factory Reset now
  clears controller persistence and performs read-only discovery/readback when
  no matching fingerprint profile exists; Generic factory-value UI actions are
  unavailable.
- Add a constrained `generic_acp_classic` profile for the confirmed
  `0x8888:0x1719` transport, with catalog-discovered Noise Suppressor, Virtual
  Bass, PreEQ, and Full-Band Music DRC support.
- Preserve the A800X as a fixed `0x8888:0x171E` profile with byte-compatible
  PreEQ/DRC frames, existing NVS persistence, and all five established modules.
- Select and release the confirmed USB interface dynamically and rebuild the
  active DSP profile after hot-plug events.
- Add schema-aware PreEQ and DRC adapters, capability-driven API/UI behavior,
  and strict Generic persistence boundaries.
- Add host regression coverage for transport setup, ACP discovery, Classic DRC,
  and direct A800X frame comparison against the 0.3.7 baseline.

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
