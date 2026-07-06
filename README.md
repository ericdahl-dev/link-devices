# link-devices

Ableton Link → mixer / MIDI / analog **tempo-sync hardware devices**. ESP32
firmware family. Carved from the `behringer` monorepo (2026-07-04, history
preserved; recovery tag `pre-split-2026-07-04` on the old repo).

## Devices

- **X32Link** (`X32Link/`) — the shipped device. ESP32-S3 firmware: reads tempo
  from **Ableton Link** or **USB-MIDI clock** and writes the matching delay time
  to a Behringer XR18/X32 FX slot over OSC. Also drives a 1.47" touch LCD
  (Waveshare board), **USB-MIDI clock OUT** (LNK-027), and a planned analog
  Eurorack sync (LNK-028). Runtime source is chosen from the web/touch config UI.
- **P4Hub** (`P4Hub/`) — the "pro" hub tier, in active development
  (ESP32-P4-NANO, native ESP-IDF). Turns a Link session into phase-accurate MIDI
  for real hardware: **USB-MIDI host** clock + transport out (drive gear like a
  Blokas Midihub directly), **phase-locked downbeat** sync, four Multiclock-style
  outputs with per-output division / phase nudge / **swing**, an audible
  metronome, a rack-panel web UI with **live (no-reboot) timing config**, and
  **MIDI clock IN** detection (publishing it back into Link is next). Reuses this
  repo's pure modules unchanged. See [`P4Hub/README.md`](P4Hub/README.md).
- **X32MidiClock** (`X32MidiClock/`) — legacy standalone MIDI-clock sketch;
  superseded by X32Link (retirement tracked in LNK-024). Shares C files with
  X32Link via symlinks.
- **X32_emulator** (`X32_emulator/`) — on-device X32 OSC emulator used for
  integration tests (also consumed by the CLI tests in the `behringer` repo).

## Architecture

Pure-C logic (host-tested with Unity) + thin Arduino glue — see `AGENTS.md` and
`docs/adr/0003-firmware-pure-c-glue-split.md`. All the interesting logic (Link
gossip parse, ping/pong measurement + its orchestrator, bar/phase math, the tick
generator, config) is **Arduino-free and portable across hardware targets** —
which is exactly what makes the P4 tier cheap to spin up. `docs/adr/0004` records
the touch-vs-web config split.

## Build / test

- **Host tests:** `make -C test` (Unity is vendored in `lib/unity/` — no submodule).
- **Firmware:** `arduino-cli compile --fqbn 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi' X32Link` (see `AGENTS.md`; board flag goes in `X32Link/build_opt.h`, kept empty at HEAD).

CI (`.github/workflows/ci.yml`) runs the host suite + compiles X32Link (headless + touch).

## Tasks

Own Ordna tracker in `tasks/` (prefixes: `LNK-` X32Link, `P4-` P4Hub, `ARC-`
cross-cutting firmware architecture, `ESP-` emulator, `MCK-` X32MidiClock;
`.ordna/config.yaml`). The `behringer` monorepo keeps its `T-*` CLI tracker
separately.
