# link-devices

Ableton Link → mixer / MIDI / analog **tempo-sync hardware devices**. ESP32
firmware family. Carved from the `behringer` monorepo (2026-07-04, history
preserved; recovery tag `pre-split-2026-07-04` on the old repo).

## Devices

- **X32Link** (`X32Link/`) — the shipped device. ESP32-S3 firmware: reads tempo
  from **Ableton Link** or **USB-MIDI clock** and writes the matching delay time
  to a Behringer XR18/X32 FX slot over OSC. Also drives a 1.47" touch LCD
  (Waveshare board), **USB-MIDI clock OUT** (LNK-027), a planned analog
  Eurorack sync (LNK-028), and **web-based OTA updates** (LNK-034, upload a
  .bin at `/update`). Runtime source is chosen from the web/touch config UI.
- **KitchenSync** (`KitchenSync/`) — the "pro" hub tier, in active development
  (ESP32-P4-NANO, native ESP-IDF). Turns a Link session into phase-accurate MIDI
  for real hardware: **USB-MIDI host** clock + transport out (drive gear like a
  Blokas Midihub directly), **phase-locked downbeat** sync, four Multiclock-style
  outputs with per-output division / phase nudge / **swing**, an audible
  metronome, a WS2812 visual metronome, a rack-panel web UI with **live
  (no-reboot) timing config**, **web-based OTA updates** (P4-017, dual-slot),
  **MIDI clock IN** detection, and **Follow Beat** (P4-020) — mic-based tempo
  detection from the onboard ES8311 codec, display-only for now. Reuses this
  repo's pure modules unchanged. See [`KitchenSync/README.md`](KitchenSync/README.md).
- **X32_emulator** (`X32_emulator/`) — on-device X32 OSC emulator used for
  integration tests (also consumed by the CLI tests in the `behringer` repo).
- **LoraLink** (`LoraLink/`) — a pair of ESP32-S3+SX1262 LoRa boards that relay
  the Link session BPM out of WiFi range for loose (non-phase-accurate) FX
  tempo control. See [`LoraLink/README.md`](LoraLink/README.md).
- **LinkAudioPoC** (`LinkAudioPoC/`) — a **proven spike, not a product**
  (P4-032). Streams the P4's onboard mic into an Ableton Live 12.4 session as a
  **Link Audio** channel, sample-rate-adaptive and calibrated to ~1 ms of Live's
  grid. Deliberately separate from KitchenSync: it links the **GPLv2** Ableton
  Link SDK, which cannot ship in closed hardware without a commercial licence
  from Ableton — a business gate nobody has cleared yet. See
  [`LinkAudioPoC/README.md`](LinkAudioPoC/README.md).

## Architecture

Pure-C logic (host-tested with Unity) + thin Arduino glue — see `AGENTS.md` and
`docs/adr/0003-firmware-pure-c-glue-split.md`. All the interesting logic (Link
gossip parse, ping/pong measurement + its orchestrator, bar/phase math, the tick
generator, config) is **Arduino-free and portable across hardware targets** —
which is exactly what makes the P4 tier cheap to spin up. `docs/adr/0004` records
the touch-vs-web config split.

## Build / test

- **Host tests:** `make -C test` (Unity is vendored in `lib/unity/` — no submodule).
  Every test also depends on every header, so a changed constant can never leave
  a stale binary passing locally while CI fails.
- **Firmware (Arduino targets):** `arduino-cli compile --fqbn 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi' X32Link` (see `AGENTS.md`; board flag goes in `X32Link/build_opt.h`, kept empty at HEAD).
- **Firmware (ESP-IDF targets):** `idf.py build` in `KitchenSync/` or
  `LinkAudioPoC/` (esp32p4). WiFi credentials live in a gitignored `sdkconfig`,
  never committed.
- **OTA (no USB after first flash):** both X32Link and KitchenSync accept a `.bin`
  at `/update` over WiFi. See [`docs/ota-deployment.md`](docs/ota-deployment.md).

CI (`.github/workflows/ci.yml`) runs the host suite + emulator seam tests and
compiles X32Link (headless / touch / QT Py+battery), X32_emulator, LoraLink
(sender + receiver), and KitchenSync (ESP-IDF). `master` is protected: all four
checks must pass, on an up-to-date branch, before a PR can merge.

## Tasks

Own Ordna tracker in `tasks/` (prefixes: `LNK-` X32Link, `P4-` KitchenSync, `ARC-`
cross-cutting firmware architecture, `ESP-` emulator, `MCK-` the retired
standalone MIDI-clock sketch, historical; `.ordna/config.yaml`). The `behringer` monorepo keeps its `T-*` CLI tracker
separately.

## Credits

This firmware builds on third-party work — Unity, ESP-IDF + Espressif components,
Arduino-ESP32, RadioLib, U8g2, LovyanGFX, and the web-UI fonts. The shipping
firmware (X32Link, KitchenSync, LoraLink) speaks the Ableton Link protocol via a
**clean-room implementation** in `X32Link/link_protocol.c` — receive-only, no
Ableton code. The `LinkAudioPoC/` spike is the one exception: it links Ableton's
own **GPLv2** SDK (vendored, gitignored, patched — see
`LinkAudioPoC/patches/`), which is why it is quarantined outside the product
firmware. See [`THIRD_PARTY.md`](THIRD_PARTY.md) for authors, licenses, and
where each is used.
