# Ticket: Generic ACP Classic Mode

## Status

done

## Objective

Add safe, capability-driven support for the confirmed 0x8888:0x1719 ACP
device while preserving the A800X behavior and wire protocol.

## Inputs

- User specification received 2026-07-18.
- Clean baseline `3e0fe0c` (`v0.3.7`).
- Captured device IDs, catalog mapping, module layouts, and DRC semantics.

## Deliverables

- Runtime USB transport and device profile modules.
- Catalog discovery and structural validation.
- Dynamic effect routing with schema adapters.
- Capability-aware API/UI and profile-scoped persistence behavior.
- Executable local protocol/regression tests and successful firmware build.
- Review-ready uncommitted diff; no deployment, push, tag, or release bump.

## Acceptance

- A800X fixtures remain byte-identical and retain their API/NVS behavior.
- Unknown USB devices are not claimed.
- Generic writes target only discovered, type/name/structure-validated modules.
- Classic DRC writes preserve all untouched array elements and reject non-mode-2 state.
- Disconnect/deinit release exactly the claimed interface.
- No 0xFC/0xFD transmission path is introduced or exercised.

## Result

- Host protocol/regression tests and ESP-IDF 6.0.2 build pass.
- Deployed 0.4.0 locally by OTA; rollback validation completed successfully.
- A800X was recognized as the fixed profile on interface 0.
- Noise Suppressor, Virtual Bass, Silence Detector, PreEQ, and DRC live changes
  were confirmed by readback and restored to their initial values.
- Boot restore and native USB disconnect/reconnect both restored and confirmed
  all five modules from the existing A800X NVS profile.
- The live Web UI showed DSP/STA connected, firmware 0.4.0, and all five A800X
  cards visible and confirmed with no browser-console errors.
- No 0xFC/0xFD caller or runtime transmission was observed. No DSP flash save,
  public push, tag, or release was performed.
- Physical Generic ACP validation remains pending the NVarcher board test.
