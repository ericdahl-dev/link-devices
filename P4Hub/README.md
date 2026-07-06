# P4Hub — ESP32-P4 hub-tier firmware

The "pro" tier of the `link-devices` family: an **ESP32-P4-NANO** that turns an
Ableton Link session into phase-accurate MIDI for real hardware, and drives an
onboard metronome — the ambitious I/O the small S3 `X32Link` box can't reach.

Where `X32Link` reads a tempo and nudges one mixer FX delay, **P4Hub is a clock
hub**: it hosts USB-MIDI gear directly (no computer), locks to the Link session's
actual downbeat, fans out up to four independently-configured clock outputs, and
is configured live from a phone — no reboot to dial in timing.

> North star (`tasks/P4-001.md`): a full-featured, phase-accurate MIDI-clock /
> Ableton Link device — clock in **and** out, transport, and eventually
> bidirectional Link.

## What it does

| Capability | Ticket | Status |
|---|---|---|
| Link tempo in over WiFi (via the onboard ESP32-C6, ESP-Hosted) | P4-004 | ✅ on hardware |
| **USB-MIDI host** clock out — drive gear (e.g. Blokas Midihub) directly | P4-003/005 | ✅ on hardware |
| **Phase-locked downbeat** — ghost-xform sync to the session beat, not just its rate | P4-009 | ✅ on hardware |
| MIDI **transport** — Start/Stop from the Link play state | P4-008 | ✅ on hardware |
| Audible **metronome** — beat click + bar-1 accent out the onboard speaker | P4-006 | ✅ on hardware |
| Metronome **volume + click voice** (Tone / Click / Wood) | P4-012 | ✅ on hardware |
| **Multiclock**-style per-output config — division, phase nudge, cable | P4-010 | ✅ on hardware |
| Per-output **swing / shuffle** | P4-013 | ⏳ built + host-tested; ear-test pending |
| **Live config** — timing/swing audible instantly, no reboot (`/live` + steppers) | P4-015 | ⏳ built; on-device check pending |
| Rack-panel **web UI** — config + live status | P4-007 | ✅ on hardware |
| **MIDI clock IN** → detected BPM (displayed) | P4-011 s1 | ⏳ built + host-tested; needs a clock source to verify |
| MIDI clock IN → **publish into Link** (P4 as tempo-setting peer) | P4-011 s2 | ⬜ not started |
| **Web-based OTA update** — push a `.bin` at `/update`, no serial/USB needed | P4-017 | ⏳ built; on-device flash-and-boot check pending |

## Signal flow

Pure, host-tested logic (`■`) composed by thin ESP-IDF glue (`▷`). Every `■`
module is reused unchanged from `../X32Link` (see Architecture).

```
        WiFi (C6)                          Type-A USB host
           │                                   │   ▲
           ▼                                   ▼   │ 0xF8 in
   ▷ wifi_link ──■ link_protocol ─► LinkTimeline   │
   ▷ link_measure_io ──■ link_measurement ─► GhostXForm
           │                                       │
           └──────────────► ■ beat_source ◄────────┘
                        (phase-locked session beat
                         vs free-run ■ beat_clock)
                                │  beats
              ┌─────────────────┼──────────────────┐
              ▼                 ▼                   ▼
     per output ×4        ■ transport        ■ metronome
   ■ clock_output       (play → Start/Stop)  (beat → click)
   swing_warp →                │                   │
   phase nudge →               ▼                   ▼
   ■ clock_ticker(ppqn)  ■ usb_midi_pack    ▷ metronome_audio
        │                      │              (ES8311 speaker)
        └──► ■ usb_midi_pack ──┤
                               ▼
                     ▷ usb_midi_host  ──►  bulk OUT to device

   MIDI clock IN:  ▷ usb_midi_host (0xF8 in) ─► ■ midi_clock_in
                   ─► ■ midi_bpm_calc ─► detected BPM (shown in /status;
                   publishing it into Link is P4-011 stage 2)
```

The heart is **`beat_source`**: it picks the phase-locked session beat (once a
`GhostXForm` is committed — P4-009) or the free-running local accumulator, and
signals when the grids must re-prime. **`clock_output`** then warps each output's
beat for swing, applies its phase nudge, and quantizes to its division before
`usb_midi_pack` encodes the pulse.

## Architecture

Per [ADR-0003](../docs/adr/0003-firmware-pure-c-glue-split.md), all the
interesting logic is **pure C with no framework dependency**, host-tested with
Unity in `../test`, and **shared unchanged** with the S3 `X32Link` firmware. The
pure engine physically lives in `../X32Link/` and is compiled into P4Hub by
relative path from `main/CMakeLists.txt` — single source, no forks
([ADR-0006](../docs/adr/0006-shared-pure-c-lives-in-arduino-sketch-root.md)
explains why it lives in the sketch root rather than a neutral `shared/`).

- **Shared pure engine** (`../X32Link/`): `beat_clock`, `beat_source`,
  `clock_ticker`, `clock_output`, `swing`, `transport`, `metronome`,
  `metronome_voice`, `usb_midi_pack`, `midi_bpm_calc`, and the Link stack
  (`link_protocol`, `link_measurement`, `link_measurement_session`, `link_phase`).
- **P4Hub-local pure** (`main/`): `p4hub_config`, `p4hub_form` (POST-body
  decode/patch), `p4hub_status` (JSON), `midi_clock_in`.
- **P4Hub glue** (`main/`): `p4hub_main` (the 1 ms clock task), `wifi_link` +
  `link_measure_io` (sockets), `usb_midi_host` (USB), `metronome_audio`
  (ES8311/I2S), `p4hub_web` (HTTP server), `p4hub_config_nvs`.

### Why ESP-IDF (not Arduino like X32Link)

The hub features need ESP-IDF components the Arduino core doesn't expose — the
`usb_host` stack (USB-MIDI host) and `esp_hosted` / `esp_wifi_remote` (WiFi via
the C6 co-processor). So P4Hub is a native ESP-IDF app while `X32Link` stays an
arduino-cli sketch ([ADR-0005](../docs/adr/0005-per-target-firmware-framework.md)).

## Hardware

- **Waveshare ESP32-P4-NANO** (RISC-V). Early silicon (chip rev v1.3);
  `sdkconfig.defaults` drops the ESP-IDF minimum P4 revision to v1.0 so it boots.
- **ESP32-C6** companion for WiFi over SDIO (ESP-Hosted).
- **ES8311 codec + NS4150B amp** on the onboard speaker for the metronome
  (I2C @0x18, PA-EN GPIO53, I2S MCLK/BCLK/WS/DOUT/DIN = 13/12/10/9/11).
- **Type-A USB** OTG port hosts the downstream USB-MIDI device.

## Build / flash / test

Requires ESP-IDF v5.5+ (`. $IDF_PATH/export.sh`).

```sh
cd P4Hub
idf.py set-target esp32p4          # first time only
idf.py build
idf.py -p <port> flash monitor
```

Host-test the pure logic (fast, runs on the dev box — no hardware):

```sh
make -C ../test                    # Unity suites, incl. every P4Hub pure module
```

WiFi credentials live in `sdkconfig` (gitignored — never committed);
`sdkconfig.defaults` stays creds-free.

## Configuration

Open the device IP in a browser for the rack-panel UI: live status (session
tempo, peers, USB device, **MIDI Clock In**, pulses out) plus the config form.

Two write paths:

- **`POST /live`** — timing controls (per-output enable / cable / division /
  **NUDGE** / **SWING**, clock-out, metronome accent) apply **in place on the next
  tick, no reboot**. The `+`/`−` steppers post here, so you dial timing by ear.
- **Write & Reboot (`POST /save`)** — persists everything to NVS and restarts.
  Required for **WiFi credentials** and **metronome enable / volume / voice** (a
  reconnect / codec re-init), which `/live` never touches.

> ⚠️ After flashing a build that changed the config struct layout, do **one**
> Write & Reboot to rewrite NVS cleanly — otherwise old blobs misload. P4-014
> (versioned NVS) will remove this footgun.

### Firmware update (OTA, P4-017)

`/update` uploads a compiled `.bin` (`idf.py build`'s
`build/p4hub.bin`) straight into the inactive OTA slot and boots into it — no
USB cable needed once the device is on WiFi. The partition table
(`CONFIG_PARTITION_TABLE_TWO_OTA`) is two equal-purpose app slots + `otadata`,
no factory partition, so every push targets whichever slot isn't currently
running. From the CLI: `curl --data-binary @build/p4hub.bin http://<device-ip>/update`.

## Status & roadmap

The Link→MIDI spine (tempo + phase in, clock + transport out, metronome,
multiclock) is **built and hardware-verified**. Recently landed and awaiting an
on-device pass: per-output **swing** (P4-013), **live config** (P4-015), and
**MIDI clock IN → BPM** detection (P4-011 stage 1).

Next:

- **P4-011 stage 2** — publish the detected MIDI-in tempo into Link (the P4 as a
  tempo-setting peer) + a Link-vs-MIDI-in source arbiter. The hard part; verified
  against the Link reference-probe rig.
- **P4-014** — versioned NVS so config survives struct changes without a manual
  rewrite.
