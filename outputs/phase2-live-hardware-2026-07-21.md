# Phase 2 live hardware report — 2026-07-21

## Device and discovery

- Device: Generic ACP Classic / NVarcher (`0x8888:0x1719`)
- Catalog: 26 effects, dynamic IDs
- Music Phase: discovered at `0x8E`, 4-byte state
- Music Delay: discovered at `0x8B`, confirmed 8-byte state
- Virtual Bass Classic: not present; capability false, no write attempted

## Live checks

- Music Phase: original normal; one inverted write confirmed by immediate
  readback; restored to normal and confirmed.
- Music Delay including HQ: confirmed layout is enable, left delay, right
  delay, HQ. Final write/readback restored the documented original state:
  disabled, 50 ms on both channels, HQ enabled. API reported
  `confirmed:true` and `saved:true`.
- No DSP flash-save command (`0xFC`/`0xFD`) was sent.

## Regression and builds

- Generic ACP and NVS host tests: PASS
- A800X frame comparison against commit `55ea822`: PASS, byte-identical
- A800X fixed effect mapping/default path: unchanged; no inferred mapping added
- JavaScript syntax and `git diff --check`: PASS
- ESP32-S3 Mini build: PASS, `0xf43f0`, 31% reserve
- Hardware-profile build: PASS, `0xf51e0`, 30% reserve

## Scope note

A separate A800X VB Classic or Music Delay address is not confirmed in the
repository. In accordance with the no-guess boundary, those capabilities were
not enabled for the fixed A800X profile. Generic controls appear only after
successful catalog discovery and state validation.
