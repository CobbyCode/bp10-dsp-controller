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
- [x] Add capability-driven Phase and Delay/HQ controls with dynamic Generic
  IDs, ESP-NVS persistence, UI cards, short host/build regression, and live
  NVarcher write/readback. Keep unavailable VB Classic hidden and unwritten.
- [x] Add a shared versioned A800X/Generic ACP configuration format, bind
  Generic imports to the active schema fingerprint, enable matching UI/API
  flows, and cover both profiles with host tests.
- [x] Finalize release 0.4.2 with host tests, the canonical ESP32-S3 4 MB
  Flash / 2 MB PSRAM image, checksum, local commit, and tag. Do not push.

## Current step

Release 0.4.2 is validated, committed, and tagged locally. No push performed.

## Baseline

- Commit/tag: `3e0fe0c` / `v0.3.7`
- Archive: `/home/pbclaw/ai/projects/a800x-generic-acp-baseline-0.3.7/`
- Pre-existing untracked file: `main.zip` (must remain untouched)

## Release

Version is `0.4.2`. Local commit and annotated tag are authorized. No push.
