# KitchenSyncTouch — dedicated sketch design (2026-07-10)

Designed with the Embedded Firmware Engineer. KitchenSync Touch is a GTM device
(customer: Dan) to control a Roland RC-505 from Ableton Link. Target board:
Waveshare **ESP32-S3-Touch-LCD-1.47**. It grabs Link (receive-only), sends MIDI
clock + transport on a **DIN jack** (GPIO11, `din_midi_out`) and USB-MIDI, and has
a **touch UI for transport**. **No OSC, no X32 mixer** anything.

Forked cleanly out of `X32Link/` (which STAYS the X32-mixer product). KitchenSync-
Touch **reuses X32Link's shared modules by committed relative symlink** (the proven
ADR-0006 mechanism — `X32_emulator/fw_version.h` and all of LoraLink's `link_*`
already do this; arduino-cli follows symlinked `.c/.cpp/.h` in a flat sketch root,
CI compiles them). It does **not** copy them.

## The coupling lever

A symlinked reused `.cpp` resolves `#include "config.h"` / `#include "app_config.h"`
to the **sketch-local** copies. So we sever X32 by giving KitchenSyncTouch its OWN
`config.h` + `app_config.*` (never symlinked) and only symlinking TUs whose headers
are clean (no OSC/mixer includes). `app_config.h` and `config.h` are the poisoned
ones (mixer_ip/model/fx_slot, `config_model_port` = the OSC port) — kept local.

## Sketch layout (flat; `.ino` basename == dir)

New files: `KitchenSyncTouch.ino`, `config.h` (board flag + timing consts + KS
defaults, NO mixer defaults), `app_config.{h,c}` (trimmed: wifi + quantum_beats +
clock_enable + transport_enable; NO mixer/OSC/fdr), `app_config_nvs.cpp` (NVS
namespace **`kstouch`**, not `x32link`), `ktouch_tempo.cpp` (Link-only adapter
behind `tempo_source.h`), `ktouch_display.cpp` (status + transport screens),
`ktouch_ui.{c,h}` (PLAY/STOP hit-rects, host-tested), `ktouch_transport.{c,h}`
(single-slot intent mailbox), `ktouch_midi_out.cpp` (one 1 ms writer: clock+transport
→ DIN+USB), `ktouch_web.{cpp,h}` (trimmed form, reuses `ui_chrome`), `sketch.yaml`
(profile pinning FQBN + LovyanGFX version).

Committed relative symlinks into `../X32Link/`: `fw_version.h`, `tempo_source.h`
(header only), `tempo_snapshot.*`, `link_protocol.*`, `link_listener.*`,
`link_measurement*.*`, `link_measure_pump.*`, `link_phase.*`, `session_timeline.*`,
`clock_ticker.*`, `midi_clock_out.*`, `midi_clock.*` (+ send_fa/fc added),
`din_midi_out.*`, `transport.*`, `transport_launch.*`, `wifi_conn_policy.*`,
`axs5106l.*`, `touch_ui.*` (after the pure/x32 split), `ui_chrome.*`,
`web_status_json.*`.

Deliberately NOT symlinked (X32-coupled / unneeded, so not compiled): `web_config.cpp`,
`app_config.*`, `config.h`, `touch_display.cpp`, `osc_*`, `bpm_publisher.*`,
`led_phase.*`, `wifi_down_blink.*`, `beat_*`, `metro*`, `swing.*`, `clock_output.*`,
`battery_*`, `midi_bpm*`, `usb_midi_pack.*`.

## New runtime path (touch → wire)

```
AXS5106L → ktouch_display dispatch → ktouch_ui hit-test (PLAY/STOP rects)
  → ktouch_transport_post(intent)             [portMUX 1-slot mailbox]
  → ktouch_midi_out task (1 ms, SINGLE writer to USB + UART):
       beats = tempo_source_beats_now()        [<0 when phase invalid]
       if clock_enable: n = midi_clock_out_ticks_due(&sched,beats,BURST)
            for n: din_midi_out_byte(0xF8); midi_clock_send_f8();
       intent = ktouch_transport_take()
       out = transport_launch_step(&tl, intent, beats, quantum, beats>=0)
       if START: din_midi_out_byte(0xFA); midi_clock_send_fa();
       if STOP:  din_midi_out_byte(0xFC); midi_clock_send_fc();
```

Single task = single writer → no USB/UART interleave race. `midi_clock_send_fa/fc`
are the only additions to shared `midi_clock.cpp` (X32Link never calls them, inert).
**ESP-015 ungate applies to DIN:** DIN F8/FA/FC gate ONLY on the musical decision,
never on USB-host presence — the RC-505 runs headless.

## Build / board

- **Board flag `#define BOARD_WAVESHARE_S3_TOUCH_LCD_147` in `config.h`**, NOT
  `build_opt.h`. A tracked `#define` in a normal source is in the dependency graph,
  so it can't go stale in the arduino-cli cache the way a `build_opt.h` edit did
  (needed `--clean`). Single-board product, so no build_opt juggling.
- `sketch.yaml` profile pins FQBN `esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,
  FlashSize=16M,PSRAM=opi` + `LovyanGFX` version (no `@latest`). arduino-cli
  sketch.yaml has no build-flags key, which is the other reason the -D lives in
  config.h. Build: `arduino-cli compile --profile s3touch KitchenSyncTouch`.
- Verify the partition scheme gives app0/app1 for `/update` OTA (pin
  `PartitionScheme=min_spiffs` in the FQBN if the default doesn't).

## Increments (each compiles + bench-verifiable)

1. **Boots, grabs Link, clocks DIN+USB, shows tempo. No transport/web/OSC.** New:
   `.ino`, `config.h`, `app_config.*`, `app_config_nvs.cpp` (wifi+quantum+clock),
   `ktouch_tempo.cpp`, `ktouch_display.cpp` (status: BPM + phase + IP). Symlink the
   Link stack + clock + `din_midi_out` + `axs5106l` + pure `touch_ui` + `fw_version`.
   May temporarily symlink `midi_clock_out_io.cpp` for the clock task; WiFi creds
   from a compile-time `secrets.h` (LoraLink pattern) — no captive portal yet.
   Verify: LCD shows live BPM; analyzer on GPIO11 decodes 0xF8; DAW sees USB clock.
2. **Transport touch screen.** `ktouch_ui`, `ktouch_transport`, `ktouch_midi_out`
   (swap OUT the temp `midi_clock_out_io` symlink — one writer only). Add
   `midi_clock_send_fa/fc`. Symlink `transport_launch`. Verify: PLAY mid-bar → 0xFA
   on the next bar (analyzer), STOP → immediate 0xFC; RC-505 starts/stops in time.
3. **Trimmed web + captive portal + OTA.** `ktouch_web` (wifi + clock + transport +
   quantum; reuses `ui_chrome_css/js` + `ui_result_page/ui_update_page`). AP
   `KSTouch-Config`. `/status`, `/update`. Delete Inc1 compile-time creds.
4. Polish: touch quantum ±, phase-dot colour, mDNS (ESP-014), OTA rollback.

## Prereq refactor in X32Link (Inc2)

Split `touch_ui.{c,h}`: `ui_apply_settings_tap` + `ui_field_t` (UI_F_MODEL_*/SLOT_*)
are X32 — move to `touch_settings_tap.{c,h}`, leaving `touch_ui` pure so both sketches
symlink it. (Fallback: copy the 5 geometry helpers — but that duplicates host-tested
code, against reuse-don't-copy.)

## Footguns

- **Duplicate-symbol:** never symlink `tempo_source.cpp` AND ship `ktouch_tempo.cpp`
  (both define `tempo_source_*`). Symlink the HEADER only. Same for
  `midi_clock_out_io.cpp` vs `ktouch_midi_out.cpp` (both spawn a clock task).
- **Include-path leak:** a symlinked reused `.cpp` picks up the sketch-local
  `config.h`/`app_config.h`; if it references an X32-only field it won't compile —
  that IS the tripwire that a "shared" module is actually X32-coupled. Keep those
  headers local, audit symlinked headers for transitive `#include "osc_*"`/mixer.
- **NVS namespace:** use `kstouch`, not `x32link` (a reflashed board would read X32
  keys into a struct without them).
- **Two-writer race:** don't keep the Inc1 clock task AND add a transport task —
  collapse to the single `ktouch_midi_out` writer in Inc2.
- **`midi_clock.cpp` USB name** is hardcoded `USBMIDI("X32Link")` (DAW port name).
  Shared file — renaming changes X32Link too. Accept or parameterize; don't fork.
- **Stale cache:** board flag in config.h avoids it; if you touch a build_opt.h,
  `arduino-cli compile --clean`.
- **check_docs.sh:** reference KitchenSyncTouch files in AGENTS.md by repo-relative
  path (`KitchenSyncTouch/…`), else the guard resolves under `X32Link/` and fails.
- **CI:** add a `KitchenSyncTouch` compile job (`arduino-cli compile --profile
  s3touch KitchenSyncTouch`), reuse the arduino cache.
