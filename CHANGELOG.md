# Changelog

## 0.5.2 - 2026-07-26

- UI: rename Music Delay to Delay and Music Phase to Phase in the
  module summaries so the path-agnostic Generic ACP labels match the
  existing Pre EQ and Out EQ naming.
- UI: highlight the active Music or Rec path button in the same accent
  colour used by primary actions, by adding a `.btn-secondary.active`
  style next to the existing `.btn-secondary` rule.

## 0.5.1 - 2026-07-25

- Remove profile-dependent DRC mode lock on AIYIMA A800X: all modes 0-6
  (Full Band, 2 Band, 2 Band + Full Band) are now selectable through the
  same shared DRC view, matching the Generic ACP profile behaviour.
- Expand the A800X DRC view codec so that non-zero modes expose the correct
  lower, upper, full band visibility, crossover, and Q controls.
- Keep the A800X DRC wire protocol (54-byte four-path format) unchanged;
  all mode writes, readbacks, persist, import, and export operate through
  the existing full-frame codec.
- Make the DRC Load Factory Values button active in every mode. Clicking it
  resets the local form to mode 0 Full Band with the known A800X factory
  values without writing to the DSP until Apply.
- UI label: PreEQ → Pre EQ for consistency with Out EQ.

## 0.5.0 - 2026-07-24

- Add profile-driven Music and Rec DSP paths for compatible Generic ACP
  devices, with independent capabilities, state, editing, verification, and
  ESP-NVS persistence.
- Add Music and Rec Pre EQ, Out EQ, DRC, Phase, Delay, Virtual Bass, and
  related controls when the discovered device schema reports them available.
- Add Generic USB Out Gain with full readback and verification.
- Replace the former single-layout DRC UI with one shared, mode-aware
  implementation. Full Band, Lower/Upper, optional additional Full Band,
  crossover, and Q controls are derived from the confirmed DSP mode.
- Keep AIYIMA A800X compatibility through the same profile- and schema-driven
  DRC architecture while retaining its validated 54-byte four-path wire codec.
- Extend configuration export, import, full Apply/Verify, and restore to
  independent Music and Rec paths and reject mismatched device/schema
  fingerprints.
- Keep confirmed DSP readback baselines separate from local Pre EQ, Out EQ,
  and DRC drafts so periodic polling cannot discard unapplied edits.
- Restore complete STA/system status reporting and single-path A800X module
  visibility in the updated frontend.

## 0.4.3 - 2026-07-22

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
