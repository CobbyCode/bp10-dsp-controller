# TASKS

## Goal

Keep the AIYIMA A800X path protocol-compatible while adding a constrained
`generic_acp_classic` mode for the confirmed 0x8888:0x1719 device.

## Safety boundaries

- No public push, DSP firmware update, or DSP flash save. A800X deployment is
  authorized solely for the final 0.4.0 hardware acceptance.
- Never send 0xFC/0xFD or probe unknown effect addresses/selectors.
- Generic support is limited to Noise Suppressor, Virtual Bass, one PreEQ,
  and Full-Band Music DRC.
- Existing A800X NVS data and A800X wire frames must remain unchanged.

## Progress

- [x] Restore and archive the rejected 0.4.0 WIP.
- [x] Archive the clean 0.3.7 baseline and run its project build.
- [x] Attempt the documented baseline test command and record that it is not wired.
- [x] Capture executable A800X regression fixtures.
- [x] Add runtime USB transport selection and exact interface cleanup.
- [x] Add fixed A800X and discovered Generic ACP device profiles.
- [x] Add catalog discovery and structural module validation.
- [x] Add schema-aware PreEQ and DRC adapters with dynamic effect IDs.
- [x] Integrate profile lifecycle, API capabilities, UI visibility, and persistence limits.
- [x] Run local tests, A800X byte-regression checks, and the firmware build.
- [x] Present the complete uncommitted diff for review.
- [x] Complete A800X hardware acceptance over Serial, API, Web UI, boot restore,
  and native USB hot reconnect. Generic ACP physical acceptance remains pending
  the NVarcher hardware test.
- [x] Fix the Generic defaults leak, verify the NVarcher Factory Reset as a
  zero-write readback, retain the unchanged A800X frame/default regression, and
  pass host tests plus the ESP32-S3 Mini build.

## Current step

Generic-defaults fix complete locally. Resume Phase 2 only after explicit
direction. The additional installed-controller A800X OTA hardware test is
deferred; do not push, tag, or release.

## Baseline

- Commit/tag: `3e0fe0c` / `v0.3.7`
- Archive: `/home/pbclaw/ai/projects/a800x-generic-acp-baseline-0.3.7/`
- Pre-existing untracked file: `main.zip` (must remain untouched)

## Release

Version is `0.4.0` for the approved local build and A800X acceptance. No tag,
release, or push is authorized.
