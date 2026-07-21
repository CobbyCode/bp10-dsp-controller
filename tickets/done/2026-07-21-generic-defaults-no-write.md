# Ticket: Generic defaults must never write A800X factory values

## Goal

Fix the confirmed profile-leak bug without modifying the existing Phase-1
commit. A800X keeps its known factory defaults. Generic ACP has no universal
factory defaults and must preserve the DSP's current state unless a matching
fingerprint-bound profile exists.

## Required behavior

- `dsp_model_get_default_profile()` must be profile-aware.
- A800X Factory Reset and default loading continue to use known A800X defaults.
- Generic Factory Reset clears ESP NVS/fingerprint, restarts, discovers the
  device, and reads the complete current DSP state without any DSP write.
- Generic boot/discovery performs no DSP write unless an exact stored
  fingerprint profile matches.
- Generic UI hides or disables A800X `Load Factory Values` actions.
- `Reload from DSP` is strictly read-only.
- Hard-coded `factoryPreeqFilters` in `app.js` must only apply to A800X.

## Safety

- Preserve Phase-1 commit `1940a6508e46411faa4307e37b50124d0043fcbd`.
- No push, tag, release, or DSP firmware update.
- Never send DSP flash-save commands `0xFC`/`0xFD`.
- Preserve unrelated untracked build outputs and user files.
- Commit the reviewed fix as one separate local commit.

## Verification

1. Host tests pass.
2. Mini firmware build passes.
3. NVarcher: set a PEQ clearly different from A800X and save it on the board;
   ESP Factory Reset; after reconnect the exact NVarcher PEQ is read back.
4. NVarcher logs show zero DSP writes from reset through complete readback.
5. A800X Factory Reset still produces the known A800X defaults.
6. Record commands, logs, exact readbacks, build result, and final commit.

## Expected outputs

- Profile-aware source/UI fix.
- Host-test and mini-build evidence.
- NVarcher and A800X hardware-test evidence.
- Separate local commit; no push.

## Status

accepted; ready for separate local commit

## Implementation evidence

- `dsp_model_get_default_profile()` now returns A800X defaults only when the
  active, valid device profile is `MVS_DEVICE_A800X_FIXED`; Generic/unknown is
  cleared and rejected.
- Model initialization no longer seeds the cache with A800X values before
  discovery.
- Generic boot/reconnect remains on the existing fingerprint-gated path: an
  exact stored fingerprint may be restored; otherwise it calls the read-only
  readback path.
- Factory Reset clears NVS for every profile. For A800X only, known defaults
  are staged back into the A800X NVS key for application after restart. For
  Generic, no DSP profile is staged and reconnect stays read-only.
- Web UI tracks the discovered profile, hides/disables every `Load Factory
  Values` action for Generic, guards all handlers, and scopes the hard-coded
  PreEQ array explicitly to A800X.
- Generic config import is rejected because no safe factory baseline exists.

## Verification evidence

- `tests/host/run.sh`: PASS
  - `generic_acp_host_tests: PASS`
  - `nvs_settings_host_tests: PASS`
  - `a800x_frame_baseline_compare: PASS`
- `git diff --check`: PASS
- ESP-IDF 6.0.2 ESP32-S3 Mini build: PASS
  - Build directory: `build-generic-defaults-mini/`
  - Binary: `build-generic-defaults-mini/bp10_dsp_controller.bin`
  - Size: `0xf1d10`; app partition free: `0x6e2f0` (31%)
- Generic NVarcher Factory Reset: PASS. NVS/fingerprint cleared, reconnect
  selected read-only mode, distinctive `-10 dB` PreEQ remained, and the log
  contained zero DSP writes through complete readback. Evidence:
  `outputs/generic-defaults-hardware-2026-07-21.md`.
- Fix was secured in one separate local commit; no push was performed.

## A800X regression disposition

- The known A800X default constants and A800X DSP write implementation were not
  changed. The fix only gates access to those constants by the active profile.
- `a800x_frame_baseline_compare` remains byte-identical and PASS.
- Earlier A800X Factory Reset hardware acceptance remains valid.
- Paul accepted these results for this fix on 2026-07-21. The additional test
  on the controller installed in the closed subwoofer is explicitly deferred
  to a later OTA hardware session.

## Review

- Final source/write-path review: PASS.
- Phase-1 commit `1940a65` remains unchanged.
- Separate local commit created; no push.
