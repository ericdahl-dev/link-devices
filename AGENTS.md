# link-devices — Agent Guide

## Which firmware am I touching? (read this first)

**`KitchenSync` is the primary product of this repo.** The others are *related* products
that share its engine — not scratch work, but not the thing either.

The section below describes **X32Link**, which is one of those related products, NOT the
primary one — and `X32Link/` is *also* the shared-pure-C library home. That is exactly how
a clock-box feature ends up in the mixer bridge by accident. It has happened.

**Every board is an ESP32 variant.** There is no Arduino hardware anywhere in this repo.
"Arduino" below means only the **build framework** — .ino sketches built with
`arduino-cli` against the `esp32:esp32` core — as opposed to **ESP-IDF** (CMake +
`idf.py`). Both target ESP32s.

| Directory | What it is | Chip | Framework |
|---|---|---|---|
| **`KitchenSync/`** | **THE PRIMARY PRODUCT** — the Link-native clock box: Link in → MIDI/analog clock + transport out. | ESP32-P4 | ESP-IDF |
| `KitchenSyncTouch/` | The same clock engine, related product. Two boards: the S3 touch-LCD unit, and the **ESP-025 bench rig** (classic ESP32 DevKit + two lit buttons). Pick the board in its `config.h`; build the rig with `tools/build-bench.sh`. | ESP32-S3 / ESP32 | Arduino |
| `X32Link/` | Related product: tempo → **XR18/X32 mixer** FX-delay sync over OSC. **Also the shared-pure-C home (ADR-0003/0007)** — every other sketch symlinks modules out of here. | ESP32-S3 | Arduino |
| `LoraLink/` | Related product: LoRa tempo bridge. | ESP32-S3 | Arduino |
| `X32_emulator/` | X32 console emulator, for on-device tests. | ESP32-S3 | Arduino |
| `LinkAudioPoC/` | Link Audio feasibility spike. Also vendors the **Ableton Link SDK**. | ESP32-P4 | ESP-IDF |
| `tools/linkcli/` | Host-side Ableton Link peer — **required** for any device tempo/transport test. | — | host |

The framework split is why the shared logic is **pure C** (ADR-0003): it is the only thing
both an Arduino sketch and an ESP-IDF component can compile. Firmware-specific glue is not
portable between them; the pure modules are.

**The rule:** a **clock-box** feature (clock out, transport, buttons, MIDI/analog outputs)
goes in **KitchenSync** — or KitchenSyncTouch when it's the Arduino/bench side of the same
engine. Touch `X32Link/` only for (a) a *mixer/OSC* feature, or (b) a **shared pure
module** — and a shared module lives in `X32Link/` purely because arduino-cli can't compile
outside a sketch root. That is a build constraint, not a statement about which product
matters.

## What X32Link is

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
| **Bench rig** (ESP-025) | `tools/build-bench.sh [port]` — KitchenSyncTouch on the classic ESP32 DevKit. Swaps the board flag **and** the partition table together and restores both on exit. Never hand-swap: the product's 16 MB table on the DevKit's 4 MB chip boot-loops with no app and no banner, which looks exactly like a brick |
| Bench Link peer | `cmake -S tools/linkcli -B tools/linkcli/build && cmake --build tools/linkcli/build`, then `./tools/linkcli/build/linkcli` (see **Bench Link peer** below — required for any device tempo/transport test) |
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

## Bench Link peer (`tools/linkcli`)

**The firmware cannot form a Link session on its own.** `X32Link/link_protocol.c` is a
*listener* — it parses the gossip timeline but never transmits. A device alone on the
network therefore sees `peers:0`, and `/transport` returns 200 and then silently drops
the intent. `tools/linkcli` is the Ableton stand-in that makes the session real, and it
is a prerequisite for **any** device tempo or transport test.

It is a genuine Link peer built against the vendored SDK (`LinkAudioPoC/third_party/link`
— asio is bundled at `modules/asio-standalone`, nothing to fetch). It drives
`ableton::Link` directly rather than through the SDK's `linkhut` audio engine, so there is
no CoreAudio dependency and no click track. Source: `tools/linkcli/main.cpp`.

Keys: `↑`/`↓` tempo ±1, `←`/`→` tempo ±0.1, `space` start/stop, `q`/`Q` quantum,
`p` force beat 0 onto *now* (a known downbeat to trigger the analyzer on), `s` start/stop
sync, `a` Link on/off, `x` quit. Flags: `--tempo N`, `--quantum N`, `--play`.

Two behaviours that will otherwise read as firmware bugs:

- **A peer that joins an already-playing session stays stopped.** This is Link's design,
  not a bug: start/stop is last-writer-wins by timestamp (`Controller.hpp:89`), and a
  booting peer initialises its start/stop state with `timestamp = hostTime`
  (`Controller.hpp:67`) — so its fresh "stopped" is *newer* than an earlier "playing" and
  wins. Tempo, by contrast, *is* adopted by late joiners, which makes the asymmetry
  extra confusing. **Form the session first, then press play.** A device that reboots
  mid-test comes back stopped even though the session is playing — re-press play rather
  than hunting for a transport bug.
- **It is a live peer.** Pressing play starts every Link app on the LAN (Ableton Live,
  Note on a phone). A peer count higher than expected is usually a real device, not a bug.

Ableton Link is GPLv2. A dev tool that is never distributed is unencumbered; shipping any
of it is a licensing question, not an engineering one.

## Modules (roles — the directory is the source of truth for the file list)

| Module | Job |
|---|---|
| `X32Link.ino` | app core: setup/loop, WiFi + AP fallback, FreeRTOS bpm/led tasks, factory reset |
| `tempo_source.{h,cpp}` | the input **seam** — one interface, dispatches to Link or MIDI by `input_source` |
| `link_listener.*` + `link_protocol.*` | Link adapter; `link_protocol.c` is our own ~100-line gossip parser — **receive-only, it never transmits**, so the firmware joins a session but cannot create one (see **Bench Link peer**). The vendored Ableton SDK (`LinkAudioPoC/third_party/link`) is **not** used at runtime — only by `tools/linkcli` on the host |
| `link_measurement.{h,c}` + `link_measurement_session.{h,c}` + `link_measurement_io.cpp` | Link measurement (ping/pong) client. `link_measurement.c` = pure TLV build/parse + median/offset math; `link_measurement_session.c` = pure orchestrator (LNK-031: peer targeting, re-measure, epoch-reset, watchdog — the policy where LNK-026 lived, host-tested via an action list); `link_measurement_io.cpp` = thin WiFiUDP glue executing the session's actions — pinger-only, no PingResponder |
| `midi_clock.*` · `midi_bpm.*` · `midi_bpm_calc.*` | USB-MIDI adapter; `midi_bpm_calc` is the pure, host-tested BPM math. `midi_clock` also owns the shared USBMIDI endpoint + `midi_clock_send_f8()` for clock OUT |
| `clock_ticker.{h,c}` | LNK-027/028 shared pure tick engine: quantizes `tempo_source_beats_now()` to N PPQN pulses (phase-locked, re-primes on re-origin) + a `BarReset` tracker that fires once per bar boundary (analog reset pulse). Host-tested |
| `beat_synth.{h,c}` | LNK-033 pure free-run beat generator (`60000/bpm` interval + edge-detect); `tempo_source_beat()` delegates to it. Host-tested |
| `config_persist.{h,c}` | ARC-022 pure debounce policy deciding **when** a live config edit is written to NVS: `mark()` on edit, `due()` from a low-priority poll. Config is one nvs blob, so a slider drag must collapse to one write; a quiet window settles a burst and a max-deferral bound stops a continuous stream from starving the write. Time is an argument — host-tested |
| `clock_output.{h,c}` | ARC-019 **the** clock-out derivation, all three firmwares: `clock_output_due()` applies swing (P4-013) + phase nudge + division (P4-010) over `clock_ticker` (KitchenSync's `ks_tick` fan-out), and `ClockOutput`/`clock_output_step()` is the 1ms-writer layer — owns the reset-on-invalid-phase rule, the lifetime `dropped` banking, and the shared burst cap `CLOCK_OUTPUT_MAX_BURST` (4; ESP-018 bench value — KitchenSync's plan loop deliberately runs `KS_TICK_MAX_BURST` 96 instead, catch-up over drop). Host-tested |
| `midi_clock_out.{h,c}` + `midi_clock_out_io.{h,cpp}` | LNK-027 Link→USB-MIDI clock OUT; `midi_clock_out.{h,c}` is a thin 24-PPQN adapter over `clock_ticker` (host-test shim; the writer tasks now go through `clock_output_step`, ARC-019), `midi_clock_out_io.cpp` is the 1ms FreeRTOS task + TinyUSB/DIN writes. ARC-024: carries the tick-health probe (gap/work/overruns/bursts/dropped/core + per-stage split), published as plain scalars and **never logged** — an `ESP_LOGx` in a 1 ms RT task is a blocking UART write (P4-033). Read out via `/status` |
| `bpm_tracker.*` | change/threshold detection |
| `bar.{h,c}` | ARC-003: a bar is `quantum` beats — `bar_beats()` / `bar_ms()`; one place owns beats-per-bar so non-4/4 `quantum` scales refresh/resend (was a hardcoded `4 *`) |
| `osc_out.*` / `osc_sender.*` | OSC packet build / UDP send to the mixer |
| `app_config.*` · `app_config_nvs.cpp` | config struct, validation, NVS persistence (incl. `input_source`) |
| `tempo_snapshot.{h,c}` | ARC-001 seam: atomic `{bpm,phase,valid,quantum}` — one writer (`bpm_task`), many readers (web `/status`, UI, serial); replaces the old loose `g_current_*` globals + mutex |
| `ui_chrome.{h,c}` | ARC-017 shared web chrome for both config UIs: `ui_chrome_css()` / `ui_chrome_js()` (static, sent as chunks) + `ui_result_page()` / `ui_update_page()` (snprintf builders). Pure C, no Arduino/ESP-IDF. The *forms* stay per-firmware; only the look + client plumbing is shared. Host-tested. **Never edit this to fix one page** — anything a single page needs goes in that page's own `<style>`. The P4 page (`KitchenSync/main/ks_web.cpp`) is the brand/UX reference every page mirrors; see ADR-0008 |
| `din_midi_out.{h,cpp}` | ESP-016 hardware DIN MIDI OUT on the S3 (KitchenSync Touch): HardwareSerial UART1 TX @31250 8N1 on GPIO11, mirroring `midi_clock_send_f8()` onto a 5-pin DIN so the S3 clocks DIN gear (RC-505) with no host. Arduino glue; byte timing is the pure midi_clock_out engine |
| `buttons.{h,c}` | ESP-025 pure debounce for physical buttons (bench rig). Caller reads the pin and passes the raw level, so there is no Arduino/GPIO dependency. A button held at boot is seeded as already-down and cannot fire an action on power-up. Host-tested |
| `transport_led.{h,c}` | ESP-025 lamp state for the illuminated transport buttons: dark stopped, **blinking armed**, solid running. Exists because a quantized button is invisible without it — press Play mid-bar and nothing happens for up to a bar, which reads as a dead button. Host-tested |
| `web_config.*` | rack-panel config web UI + captive portal + `/status` live-BPM endpoint + `/update` OTA firmware upload (LNK-034, Arduino core's Update library) |
| `config.h` | per-firmware constants (Link/MIDI timing, first-boot defaults) |
| `touch_display.{h,cpp}` | on-device 1.47" LCD + touch UI (raw LovyanGFX, no LVGL): status / settings / IP-keypad screens. X32Link-only, gated `HAS_TOUCH_DISPLAY` (LNK-014/015) |
| `touch_ui.{h,c}` + `axs5106l.{h,c}` | pure, host-tested UI logic: `touch_ui` = hit-testing / value formatting / config-field taps / keypad buffer; `axs5106l` = AXS5106L touch-report parser |
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
