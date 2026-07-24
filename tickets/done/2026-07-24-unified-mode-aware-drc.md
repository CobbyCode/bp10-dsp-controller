# Ticket: unified-mode-aware-drc

## Project
bp10-dsp-controller

## Goal
Eine gemeinsame, profilgesteuerte und modefähige DRC-Implementierung für
Generic Music, Generic Rec und AIYIMA A800X bereitstellen.

## Task
Alle vorhandenen DRC-Instanzen über ihren Geräteprofil-Pfad und ihre dort
erkannte Adresse lesen. Das gemeinsame 39-Byte-Schema vollständig dekodieren,
Mode und sichtbare Bänder daraus ableiten, eine gemeinsame dynamische
GUI-Komponente anbinden und danach Schreiben, Verify sowie pfadgetrennte
Persistenz ergänzen. Bestehende funktionierende Module nicht verändern.

## Input
- Generic Music: `0x8F`
- Generic Rec: `0x90`
- A800X: erkannte DRC-Adresse des jeweiligen Geräteprofils
- Gemeinsames Full-Read-Schema aus Pauls Auftrag vom 2026-07-24
- Kein 3-Band-Modus
- Kein Push oder Release

## Expected Output
- Gemeinsamer DRC-Codec und profil-/pfadgesteuerte Runtime-Logik
- Modeabhängige API und GUI für Full, Lower, Upper, Crossover und optionale Qs
- Schreiben mit vollständigem Readback-Verify
- Pfadgetrennte Persistenz
- Bestehende und neue Hosttests erfolgreich
- Devboard-Build erfolgreich
- Firmware auf `.122` geflasht
- Hardwaretest Generic Music/Rec und A800X-Regression dokumentiert

## Target Path
`projects/bp10-dsp-controller/`

## Notes
Phase und Music-/Rec-Delay nicht verändern. Keine unbekannten Effektadressen
oder Selector probeweise schreiben.

## Status
done

## Reopened
Der Abschlussstatus wurde am 2026-07-24 zurückgenommen. Die Implementierung
ordnete `mode 2` fälschlich Full Band zu, obwohl mode 1–3 Lower/Upper mit
Crossover sind. Außerdem fehlte der Mode-Selector und der Schreibpfad lehnte
Mode-Wechsel ab. Erwartet sind mode 0 Full Band, mode 1–3 Lower/Upper und
mode 4–6 Lower/Upper plus Full Band sowie Q nur in den belegten Q-Modi.

## Correction progress
- Mode table corrected for all values 0–6.
- Q fields limited to custom-Q modes 3 and 6.
- GUI mode dropdown added with ACP Workbench labels.
- Mode changes write selector `0x02`, perform a full readback, rebuild the GUI
  from the confirmed mode and persist per path.
- Host tests, A800X frame baseline, JS syntax, diff check and devboard build:
  PASS.
- Fresh build flashed to `.122`; Music mode 2 and Rec mode 1 live GUI
  screenshots captured and delivered.
- Live transitions 0/2/3/4/6 and restore to Music 2 / Rec 1: PASS.
- Ticket remains in progress pending Pauls review; no push.

## Follow-up: EQ readback curves
- PreEQ Apply consumed the obsolete flat `/dsp/state` fields after verify,
  although the current API exposes the confirmed state below `preeq`.
- PreEQ and Out EQ now both require and adopt their complete, path-specific
  `/dsp/state` object after a successful write/verify before reporting success.
- Adopting the confirmed state replaces only the matching EQ baseline, clears
  dirty state and redraws immediately. Later local edits only alter the preview.
- The embedded `app.js` cache key was advanced so the corrected logic is loaded
  after OTA without retaining the previous frontend script.
- Live on `.122`: Music and Rec, PreEQ and Out EQ Apply/baseline/local-preview
  transitions passed; original tested pregains were restored and Music selected.

## Follow-up: A800X single-path compatibility

- Fixed only the frontend capability application: a single published path now
  applies its capabilities and is selected automatically.
- Music/Rec controls remain available only for more than one path and remain
  hidden on A800X.
- Product-Final OTA app built and installed only on A800X `.123`; `.124`
  remained untouched.
- Live DOM verification restored the previous A800X cards. Unsupported Delay,
  Out EQ and USB Out Gain remain hidden.
- `drc.valid=false` was diagnosed separately: the A800X profile now declares
  the 38-byte unified schema although the device returns its established
  54-byte A800X 4-path payload. No DRC change or write was made.

## Follow-up: A800X DRC and import

- Confirmed common cause: A800X DRC `NOT READ` made import fail during DRC
  read-before-write.
- Schema-driven common DRC path now dispatches A800X `0x9A`, logical wire
  length 55 / payload 54 to the existing `a800x_4path` codec; Generic remains
  on the 38-byte unified codec.
- Import diagnostics now return
  `module | field | expected | actual | error`.
- Raw pregain zero/one is handled as the same semantic `-72 dB` A800X floor.

## Follow-up: DRC local draft polling

- The common DRC frontend now stores baseline, local draft and dirty state
  separately per device profile and path.
- Periodic DSP readbacks continue to update the confirmed baseline and module
  status while dirty, but no longer replace the form values.
- Factory values and every later local field edit set the matching draft dirty.
- Only a successful Apply + Verify adopts the confirmed readback into the form
  and clears dirty.
- Live on A800X `.123`: factory values survived a complete poll while the
  confirmed `OFF` status continued updating. Generic `.122`: Music and Rec
  drafts survived path switches independently; unchanged Apply/Verify returned
  the form to clean and the next local edit returned it to dirty.
- Backend and DSP protocol were not changed.
- `.123`: DRC read, safe disabled write/readback, restore, and unchanged
  export/import Apply/Verify/Save pass.
- `.122`: Music/Rec DRC reads and unchanged export Apply/Verify/Save pass.
- `.124` remained unchanged as the 0.4.6 reference.

## Completion

- Paul released the current hardware-tested state on 2026-07-24.
- The exact implementation state is preserved on
  `feature/generic-music-rec-clean`.
- Release 0.5.0 adds only release metadata and documentation on top of the
  accepted implementation.
