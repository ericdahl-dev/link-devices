# LoraLink — LoRa BPM relay

Relays the current Ableton Link session tempo over LoRa, for FX tempo control
out of range of the venue WiFi network. No phase/beat alignment — BPM value
only. See [`docs/plans/2026-07-09-loralink-design.md`](../docs/plans/2026-07-09-loralink-design.md)
for the full design.

## Hardware

- 2x ESP32-S3 + SX1262 915MHz LoRa dev board w/ onboard SSD1306 OLED
  ("N30" Meshtastic-compatible clone of the Heltec WiFi LoRa 32 V3 form factor).
- One board is flashed as **sender** (joins WiFi + the Link session, transmits
  BPM), the other as **receiver** (listens, displays BPM).

## Build / flash

```sh
cp LoraLink/lora_secrets.h.example LoraLink/lora_secrets.h   # fill in your WiFi creds
arduino-cli lib install RadioLib U8g2
arduino-cli compile --fqbn esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi LoraLink
arduino-cli upload  --fqbn esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi -p <port> LoraLink
```

To flash the **receiver** board, override the role before compiling:

```sh
echo '-DLORA_ROLE_OVERRIDE=LORA_ROLE_RECEIVER' > LoraLink/build_opt.h
arduino-cli compile ... LoraLink   # then upload as above
git checkout -- LoraLink/build_opt.h   # restore sender default for next time
```

## Architecture

Pure-C wire format (`lora_bpm_packet.*`) and staleness check
(`lora_freshness.*`) are host-tested with Unity (`make -C test`). The sender
reuses `link_listener.*` from `../X32Link` unchanged to join the Link session
— no new Link-parsing code. Radio (`lora_radio.*`, RadioLib) and display
(`lora_display.*`, U8g2) are thin Arduino glue, verified on-device.

`link_listener.*` and its transitive dependencies (`link_protocol.*`,
`session_timeline.*`, `link_phase.*`) appear in this folder as symlinks into
`../X32Link/` rather than copies — the Arduino/arduino-cli build only
compiles sources physically inside the sketch's own folder, so a relative
`#include "../X32Link/link_listener.h"` alone would leave the `.cpp`
unbuilt. The symlinks keep this a single unchanged source of truth.

## Status

Milestone 1: receiver only displays BPM. FX/MIDI/CV output from the received
BPM is a follow-on, not yet built.
