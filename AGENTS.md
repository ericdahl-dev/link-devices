# esp32 — Agent Guide

## What this firmware is

**Tempo → XR18/X32 FX delay sync.** One ESP32 firmware that reads a musical
tempo from one of two sources and writes the matching delay time to an FX slot
on a Behringer XR18 / X32 mixer over OSC UDP. The tempo source is chosen at
runtime from the web UI:

- **Ableton Link** — joins a Link session over WiFi as a native peer (multicast
  UDP `224.76.78.75:20808`), reading session BPM from the gossip timeline.
- **USB MIDI clock** — acts as a USB-MIDI device and derives BPM from 24-PPQN
  clock pulses from a DAW.

Also contains the **X32 emulator** (`X32_emulator/`) used for on-device testing.

## What this firmware is NOT

Not the CLI tools (`cli/`, root `T-` tasks). This is the on-board tempo→OSC
bridge and emulator only. Don't edit `cli/` when working here.

## Build / flash / test

| Action | Command |
|---|---|
| Compile | `arduino-cli compile --fqbn esp32:esp32:adafruit_qtpy_esp32s3_n4r2:PartitionScheme=min_spiffs X32Link` |
| Flash   | `arduino-cli upload  --fqbn esp32:esp32:adafruit_qtpy_esp32s3_n4r2:PartitionScheme=min_spiffs -p /dev/ttyACM0 X32Link` |
| Host tests | `cd test && make`  (Unity; pure-logic suites, runs on the dev box) |
| Emulator seam tests | `cd tests && make run` (native gcc — `x32_port`) |
| Emulator compile | `arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB,USBMode=hwcdc X32_emulator` |

Notes:
- **Board:** ESP32-S3 Super Mini (and XIAO), or a genuine Adafruit QT Py
  ESP32-S3. We compile against the `adafruit_qtpy_esp32s3_n4r2` FQBN either
  way — for the Super Mini/XIAO it's just a generic S3 profile that boots
  those boards with USB-CDC serial; for the QT Py it's the real board. The
  beat **LED is a plain LED on GPIO48, active-HIGH** on the Super Mini
  (`X32Link.ino`: `LED_PIN_NUM`; define `LED_ACTIVE_LOW` for XIAO, `LED_RGB`
  for a generic WS2812 board). On the **QT Py ESP32-S3** the beat LED is the
  onboard NeoPixel on **GPIO39**, whose level shifter needs **GPIO38** driven
  HIGH before it lights — define `BOARD_QTPY_ESP32S3` (implies `LED_RGB`,
  sets `LED_PIN_NUM`/`LED_PWR_PIN_NUM`). These are compile-time flags with no
  runtime board detection — set the right one in `X32Link/build_opt.h`
  (tracked but meant to stay empty at HEAD; edit it locally for the unit in
  hand, don't commit board-specific flags there) before compiling.
- **Battery (QT Py only):** if the unit has Adafruit's "LiPo BFF" (MAX17048
  fuel gauge, STEMMA QT) stacked under it, also define `HAS_BATTERY_GAUGE` in
  `build_opt.h` alongside `BOARD_QTPY_ESP32S3` — separate flag, since not
  every QT Py has one attached. Reads over I2C on the STEMMA QT bus (SDA=7,
  SCL=6 — the real board's default `Wire` pins), surfaced in `/status` as
  `batt_v`/`batt_pct` and shown in the web UI panel footer. See
  `battery_gauge.{h,c}` / `battery_gauge_io.{h,cpp}` / `battery_snapshot.{h,c}`.
- Native-USB boards re-enumerate `/dev/ttyACM0` on reset; serial capture is
  flaky right after flashing. The web UI `/status` endpoint is the reliable
  way to read live BPM.
- **`PartitionScheme=min_spiffs` is required for OTA** (hardware-verified
  2026-07-09): the QT Py FQBN's default scheme is `tinyuf2_noota` — no second
  app slot, so `/update` fails with `Partition Could Not be Found`. A USB
  flash that changes the partition scheme also relocates NVS → config + WiFi
  creds reset to first-boot defaults (AP fallback). Back up the config from
  the device's `/` page before repartitioning; after it, one `/save` POST
  restores it.
- **Versioning (LNK-038):** `X32Link/fw_version.h` is the one source of truth
  for `FW_VERSION` (shared by every firmware in this repo — KitchenSync via
  include path, X32_emulator via symlink; the emulator's
  `XVERSION "4.06"` is the *emulated console's* version, not ours — never
  merge the two). Devices report it on the serial boot
  banner + periodic status line, the web UI header/footer, `/status` JSON
  (`"fw"` field — use this to audit deployed units), and the `/update` OTA
  page. Release = bump `FW_VERSION` + `git tag v<FW_VERSION>` on that commit,
  so any distributed .bin traces back to source.
- **OTA deployment (LNK-034 / P4-017):** once a unit is on WiFi, push firmware
  **without USB** via `http://<device-ip>/update`. X32Link:
  `curl -F 'update=@X32Link/build/.../X32Link.ino.bin' http://<ip>/update`
  (needs `PartitionScheme=min_spiffs` on first USB flash). KitchenSync:
  `curl --data-binary @KitchenSync/build/kitchensync.bin http://<ip>/update`.
  Audit before/after with `/status` → `"fw"`. Full agent playbook:
  [`docs/ota-deployment.md`](docs/ota-deployment.md).

## Mixer output

- OSC target: `/fx/{slot}/par/01 ,f {normalized}` (delay-time parameter).
- Port: **XR18 → 10024**, **X32 → 10023** (`app_config.c:config_model_port`).
- Normalization lives in `osc_out.c:bpm_to_normalized()` — delay ms = 60000/BPM,
  clamped to 3000 ms, scaled to 0.0–1.0.

## Modules (roles — the directory is the source of truth for the file list)

| Module | Job |
|---|---|
| `X32Link.ino` | app core: setup/loop, WiFi + AP fallback, FreeRTOS bpm/led tasks, factory reset |
| `tempo_source.{h,cpp}` | the input **seam** — one interface, dispatches to Link or MIDI by `input_source` |
| `link_listener.*` + `link_protocol.*` | Link adapter; `link_protocol.c` is our own ~100-line gossip parser (the vendored `lib/link/` SDK is **not** used at runtime) |
| `link_measurement.{h,c}` + `link_measurement_session.{h,c}` + `link_measurement_io.cpp` | Link measurement (ping/pong) client. `link_measurement.c` = pure TLV build/parse + median/offset math; `link_measurement_session.c` = pure orchestrator (LNK-031: peer targeting, re-measure, epoch-reset, watchdog — the policy where LNK-026 lived, host-tested via an action list); `link_measurement_io.cpp` = thin WiFiUDP glue executing the session's actions — pinger-only, no PingResponder |
| `midi_clock.*` · `midi_bpm.*` · `midi_bpm_calc.*` | USB-MIDI adapter; `midi_bpm_calc` is the pure, host-tested BPM math. `midi_clock` also owns the shared USBMIDI endpoint + `midi_clock_send_f8()` for clock OUT |
| `clock_ticker.{h,c}` | LNK-027/028 shared pure tick engine: quantizes `tempo_source_beats_now()` to N PPQN pulses (phase-locked, re-primes on re-origin) + a `BarReset` tracker that fires once per bar boundary (analog reset pulse). Host-tested |
| `beat_synth.{h,c}` | LNK-033 pure free-run beat generator (`60000/bpm` interval + edge-detect); `tempo_source_beat()` delegates to it. Host-tested |
| `midi_clock_out.{h,c}` + `midi_clock_out_io.cpp` | LNK-027 Link→USB-MIDI clock OUT; `midi_clock_out.{h,c}` is a thin 24-PPQN adapter over `clock_ticker`, `midi_clock_out_io.cpp` is the 1ms FreeRTOS task + TinyUSB writes |
| `bpm_tracker.*` | change/threshold detection |
| `bar.{h,c}` | ARC-003: a bar is `quantum` beats — `bar_beats()` / `bar_ms()`; one place owns beats-per-bar so non-4/4 `quantum` scales refresh/resend (was a hardcoded `4 *`) |
| `osc_out.*` / `osc_sender.*` | OSC packet build / UDP send to the mixer |
| `app_config.*` · `app_config_nvs.cpp` | config struct, validation, NVS persistence (incl. `input_source`) |
| `tempo_snapshot.{h,c}` | ARC-001 seam: atomic `{bpm,phase,valid,quantum}` — one writer (`bpm_task`), many readers (web `/status`, UI, serial); replaces the old loose `g_current_*` globals + mutex |
| `ui_chrome.{h,c}` | ARC-017 shared web chrome for both config UIs: `ui_chrome_css()` / `ui_chrome_js()` (static, sent as chunks) + `ui_result_page()` / `ui_update_page()` (snprintf builders). Pure C, no Arduino/ESP-IDF. The *forms* stay per-firmware; only the look + client plumbing is shared. Host-tested |
| `web_config.*` | rack-panel config web UI + captive portal + `/status` live-BPM endpoint + `/update` OTA firmware upload (LNK-034, Arduino core's Update library) |
| `config.h` | per-firmware constants (Link/MIDI timing, first-boot defaults) |
| `touch_display.{h,cpp}` | on-device 1.47" LCD + touch UI (raw LovyanGFX, no LVGL): status / settings / IP-keypad screens. X32Link-only, gated `HAS_TOUCH_DISPLAY` (LNK-014/015) |
| `touch_ui.{h,c}` + `axs5106l.{h,c}` | pure, host-tested UI logic: `touch_ui` = hit-testing / value formatting / config-field taps / keypad buffer; `axs5106l` = AXS5106L touch-report parser |
| `midi_uart_out.{h,c}` | ESP-015 hardware MIDI OUT: UART1 TX @31250 8N1 mirroring output 0's 0xF8/0xFA/0xFC onto a GPIO alongside USB, + a once-per-bar downbeat strobe GPIO (analyzer trigger for ESP-011). Impure glue; the bytes + `plan.downbeat` are pure/tested |
| `battery_gauge.{h,c}` + `battery_gauge_io.{h,cpp}` + `battery_snapshot.{h,c}` | MAX17048 fuel gauge on the QT Py's STEMMA QT bus (Adafruit "LiPo BFF"), gated `HAS_BATTERY_GAUGE` (opt-in per unit — not every QT Py has one attached). `battery_gauge` = pure VCELL/SOC register decode, host-tested; `battery_gauge_io` = thin Wire glue (SDA=7/SCL=6, addr 0x36); `battery_snapshot` = ARC-001-shaped one-writer/many-reader seam, same as `tempo_snapshot`. Surfaced in `/status` as `batt_v`/`batt_pct` (`web_status_json`'s `has_batt` param), web UI feature-detects and shows them in the panel footer |
| `X32_emulator/` | X32 on-device emulator for integration tests |

Shared pure C lives real in `X32Link/` (ADR-0007 — arduino-cli compiles only the
sketch root, so it can't move to `shared/`) and is **compiled by path**:
KitchenSync lists it in `SRCS`, host tests in `test/Makefile`. Symlinking is only
for a second flat Arduino sketch that needs the file in its own root — currently
just `X32_emulator/fw_version.h`. The old `X32MidiClock/` sketch was retired in
LNK-024 (2026-07-08); its `midi_*` files moved into `X32Link/`.

## Key facts that must stay exact (cross-check against code if editing)

- WiFi setup must call `WiFi.setSleep(false)` — modem power-save drops the
  buffered multicast and Link silently never receives. (`X32Link.ino`)
- **Hidden SSIDs:** STA connect uses `esp_wifi_set_config` with
  `WIFI_ALL_CHANNEL_SCAN` (not plain `WiFi.begin`) so the device probes with the
  saved SSID on every channel. If every saved network fails within
  `WIFI_CONN_TIMEOUT_US` (45 s **total**) it falls back to the config AP
  (`X32Link-Config` / `KitchenSync-Setup`) — join that, re-enter SSID/pass at
  `http://192.168.4.1`, Write & Reboot. ESP32 is **2.4 GHz only**.
- **Multi-network (ESP-013, KitchenSync):** `KsConfig` holds `KS_WIFI_SLOTS`
  saved networks. `wifi_conn_policy` walks them, giving each `45 s / nslots`, so
  adding networks never delays reaching the config AP. The policy is SSID-blind:
  `ks_config_wifi_slots()` compacts empty slots away and hands it a count.
  `wifi_link` logs the joined SSID next to the IP — a device that silently joined
  a different saved network looks exactly like a routing fault.
- The `httpd` task stack is ~4 KB. Request bodies (`/save`) are **heap**
  allocated; a multi-KB buffer as a stack local panics with a stack-protection
  fault on the first save.
- **mDNS (ESP-012, KitchenSync):** the board answers at
  `kitchensync-XXXX.local` (last two MAC bytes) plus a delegated `kitchensync.local`
  alias. The WiFi MAC lives on the **C6**, not the P4's efuse, so
  `esp_read_mac(ESP_MAC_WIFI_STA)` FAILS — use `esp_wifi_get_mac()` or every unit
  names itself `-0000`. mDNS is link-local: it fixes "which address today", never
  "the laptop is on another subnet". Costs ~48 KB of flash.
- USB MIDI must enumerate **before** WiFi; Link joins multicast **after**.
  Captured by `tempo_source_pre_net()` / `tempo_source_begin()`.

## Ordna tasks

`tasks/`, prefixes: `LNK-` (X32Link — the Link tempo firmware), `MCK-`
(historical: the standalone MIDI-clock sketch, merged into X32Link by LNK-010
and its directory retired by LNK-024), `ESP-` (X32 emulator), `ARC-`
(cross-cutting firmware architecture), `P4-` (the planned ESP32-P4 hub tier). OSC node references live in
`docs/xr18-xair-osc-cheatsheet.md` and `docs/x32-osc-protocol.md`.

Non-Link ESP32 products (X32FaderDisp `FDR-`, X32SafeMutes `MUT-`, MidiOscIttt
`ITT-`, X32ToastSaver-HW `TSV-`) were pruned from this repo on 2026-07-04 — they
aren't Link tempo-sync devices. They remain in the frozen `behringer` monorepo
`esp32/` copy if ever revived.
