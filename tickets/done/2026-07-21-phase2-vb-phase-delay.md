# Ticket: Phase 2 capability-driven VB Classic, Phase, and Delay

## Baseline

- Start from local commit `55ea822`.
- Preserve both earlier commits and all unrelated untracked outputs.
- No push, tag, release, `0xFC`, or `0xFD`.

## Goal

Add four simple, separate capability-driven controls with read/write/readback
and ESP-NVS persistence:

1. Virtual Bass Classic for fixed A800X and discovered Generic ACP.
2. Music Phase only when its effect is available.
3. Music Delay only when its effect is available.
4. Music Delay HQ only when its effect is available.

## Protocol boundaries

- Generic must use catalog-discovered dynamic effect IDs.
- A800X may use only already-confirmed fixed mappings present in the repository.
- Do not infer or probe any unknown A800X address, selector, or payload.
- Do not add automatic DSP flash-save. A later explicit Generic save action is
  out of scope.

## UI and persistence

- One simple UI card per module; hide the entire card when unavailable.
- Status/data APIs must expose capability and confirmed readback state.
- Writes require immediate readback confirmation.
- Extend the existing fingerprint-bound/A800X ESP-NVS profile persistence with
  the new confirmed values while maintaining backward-safe loading.

## Verification

- Existing short host tests.
- ESP32-S3 Mini build.
- NVarcher: one live write plus readback per implemented module, then restore
  original values only when directly documented/available.
- Short A800X source/frame regression proving the established path is unchanged.
- No reconnect, cold-start, or factory-reset test series.

## Expected output

- Reviewed source/UI/test changes.
- Compact hardware/build evidence.
- One separate local Phase-2 commit; no push.

## Implementation evidence (source/host gate)

- Generic catalog IDs remain dynamic: mapped IDs are retained per discovered
  effect and only become capabilities after wire-state validation.
- Added decoders for VB Classic (8 bytes), Phase (2/4 bytes), and the confirmed
  Music Delay layout (8 bytes: enable, left delay, right delay, HQ). The
  NVarcher-observed Phase and Delay layouts are covered by host fixtures.
- Added read/write/immediate-readback APIs and capability-gated UI cards for
  VB Classic, Music Phase, and Music Delay (including HQ toggle).
- Extended values are included in the existing auto-save profile. A new
  `phase2_extended_valid` gate prevents old 55ea822 blobs (whose placeholder
  fields are zero) from causing any extended-module writes after upgrade.
- NVS loaders accept the legacy profile prefix and zero-fill appended fields;
  fingerprint matching remains mandatory for Generic restore.
- A800X profile mappings are unchanged. In particular, no VB Classic or Delay
  ID was inferred, and the existing `MVS_EFFECT_PHASE 0x96` constant was not
  activated because Phase must be discovered for this UI.
- `tests/host/run.sh`: PASS (Generic ACP, NVS settings, A800X byte baseline).
- `node --check main/www/app.js`: PASS; `git diff --check`: PASS.
- `tests/host/run.sh`: PASS; A800X frame comparison against `55ea822`: PASS.
- ESP32-S3 Mini build: PASS, `0xf43f0`, 31% application-partition reserve.
- Hardware-profile build: PASS, `0xf51e0`, 30% reserve.
- NVarcher Music Phase: PASS; normal -> inverted -> normal, each confirmed by
  immediate readback; original value restored.
- NVarcher Music Delay/HQ: PASS; confirmed write/readback and exact original
  state restored (`disabled`, `50 ms`, HQ enabled), persisted to ESP NVS.
- NVarcher VB Classic: not present in the discovered 26-effect catalog, so the
  capability stayed false and no write was attempted.
- No `0xFC`/`0xFD`, DSP flash-save, reconnect, cold-start, or factory-reset
  sequence was performed.

## Status

done
