# Generic defaults hardware verification — 2026-07-21

## Device and firmware

- Generic ACP Classic / NVarcher: VID `0x8888`, PID `0x1719`
- ESP32-S3 controller on `/dev/ttyACM0`
- No DSP flash-save command (`0xFC`/`0xFD`) was sent.

## Pre-reset readback

- Noise Suppressor: OFF, -65 dB, ratio 3, attack 5 ms, release 160 ms
- Virtual Bass: ON, 70 Hz, 20%, enhanced ON
- PreEQ: ON, pregain -10 dB, selected filter 5
- Filters: F0 OFF/HP/24 Hz/Q1/+6 dB; F1 OFF/LS/35 Hz/Q0.7002/+6 dB;
  F2 ON/PK/30 Hz/Q2/+6 dB; F3 ON/PK/35 Hz/Q3/+8 dB;
  F4 ON/PK/78 Hz/Q10/+10 dB; F5 ON/PK/64 Hz/Q8/-3 dB;
  F6 OFF/PK/90 Hz/Q15/-2 dB; F7 OFF/PK/52 Hz/Q3/+4 dB;
  F8 OFF/PK/20000 Hz/Q0.7002/0 dB; F9 ON/LP/160 Hz/Q0.9004/+6 dB.

These values are clearly different from the fixed A800X factory profile.

## Factory-reset/reconnect evidence

Observed serial sequence:

1. `Factory Reset: NVS gelöscht (A800X + Generic + WiFi + Config, kein DSP-Flash-Save)`
2. `Generic Factory Reset: keine DSP-Defaults, Reconnect bleibt read-only`
3. Generic discovery and module validation completed.
4. `Generic ACP: Kein Fingerprint-Match – read-only`
5. Complete DSP readback completed.
6. `PreEQ: enabled=1 pregain_raw=-2560 selected=5`
7. Final summary retained NS OFF, VB ON, PreEQ ON, DRC ON.

There was no profile apply, state update, module setter, `0xFC`, or `0xFD`
between reset and completed readback. Catalog queries, validation reads, and
the PreEQ read request (`A5 5A 91 00 16`) were read-only.

## Result

PASS for the Generic no-default/no-write boot path. The distinctive NVarcher
state remained present after ESP NVS/fingerprint reset and reconnect readback.
The A800X factory-reset hardware case remains pending.
