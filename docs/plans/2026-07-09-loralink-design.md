# LoraLink — LoRa BPM relay design

**Date:** 2026-07-09
**Status:** design validated, implementation pending

## Purpose

A pair of ESP32-S3 + SX1262 LoRa boards (Heltec WiFi LoRa 32 V3-family, 915MHz,
"N30" clone, OLED onboard) that relay the current Ableton Link tempo out of
range of the venue WiFi network, for loose (non-phase-accurate) FX tempo
control. Exact phase/beat alignment does not matter — only the BPM value.

## Hardware

- 2x ESP32-S3 + SX1262 915MHz LoRa dev board, onboard SSD1306-class OLED,
  1100mAh battery + protect case ("N30" Meshtastic-compatible clone of the
  Heltec WiFi LoRa 32 V3 form factor).
- Pin mapping to be confirmed against the actual board silkscreen/schematic
  once in hand — kept isolated in `lora_config.h` so it's a one-file change if
  the clone's pinout differs from stock Heltec V3.

## Architecture

New top-level directory `LoraLink/`, following this repo's pure-C-logic +
thin-glue split (ADR-0003):

```
LoraLink/
  LoraLink.ino            — thin Arduino glue: setup/loop, role dispatch
  lora_bpm_packet.c/.h    — pure: encode/decode the 4-byte packet (host-tested)
  lora_freshness.c/.h     — pure: "is the last BPM stale?" logic (host-tested)
  lora_radio.cpp/.h       — glue: RadioLib SX1262 init/send/receive
  lora_display.cpp/.h     — glue: OLED (SSD1306) status rendering
  lora_config.h           — pin maps, role selection, timing constants
```

**Role selection:** a compile-time (or NVS-stored) flag, `ROLE_SENDER` /
`ROLE_RECEIVER`, picked at boot. Each board is flashed once for its role;
runtime role-swapping is not needed.

**Sender role** reuses the existing pure Link stack **unchanged** from
`X32Link/` — `link_protocol`, `link_measurement`, `link_measurement_session`,
`session_timeline` — the same modules KitchenSync reuses. It joins WiFi, joins
the Link session, reads session tempo, and hands BPM to the LoRa send path. No
new Link-parsing code.

**Receiver role** has no WiFi/Link code — only radio + OLED. It listens for
packets and renders them.

## LoRa library & protocol

- **Library: RadioLib.** Actively maintained, clean SX1262 support, and what
  Meshtastic itself is built on — strong signal the driver works well on this
  exact hardware family. LoRaWAN (needs a gateway/network server) is not used;
  this is raw point-to-point LoRa.
- **Protocol: periodic fire-and-forget broadcast, no ack/retry.** BPM rarely
  changes and exact phase doesn't matter, so a dropped packet just means a
  briefly stale display until the next broadcast — not worth ack/retry/backoff
  complexity. LoRa's own CRC catches corruption; corrupt packets are dropped
  silently and the receiver waits for the next one.

## Packet format (4 bytes)

```c
struct lora_bpm_packet {
    uint8_t  msg_type;   // MSG_BPM = 1 (room to grow, e.g. MSG_NO_SESSION)
    uint8_t  seq;        // wraps 0-255; lets receiver notice gaps (display-only)
    uint16_t bpm_x100;   // e.g. 12000 = 120.00 BPM
};
```

`lora_bpm_packet.c` provides pure `encode()`/`decode()` functions, host-tested
with Unity like the rest of the repo.

## Data flow

**Sender loop:** poll Link tempo (already updates on peer/tempo change) → on
change or every 1-2s heartbeat → encode packet → radio send → update OLED
(e.g. "Link: 3 peers, 120.0 BPM -> TX").

**Receiver loop:** radio receive (poll or DIO1 interrupt) → decode → update
OLED (e.g. "BPM: 120.0, last update 0.4s ago") and record `millis()` of last
packet.

## Error handling & freshness

- **No Link session (sender):** OLED shows "No Link session"; either stop
  transmitting or send `MSG_NO_SESSION` so the receiver can show that instead
  of a stale BPM.
- **No packets received (receiver):** `lora_freshness.c` exposes a pure
  `is_stale(now, last_seen_ms, threshold_ms)` function. Past e.g. 5s with no
  packet, OLED shows "No signal" rather than a confidently-wrong old BPM.
- **Corrupt packet:** radio CRC drops it; receiver just waits for the next one.

## Testing

- `lora_bpm_packet`: encode/decode round-trip + boundary values (0 BPM, max
  u16, seq wraparound) — Unity, `make -C test`.
- `lora_freshness`: stale/fresh boundary conditions — Unity.
- Radio and OLED glue are hardware-dependent, verified on-device (same as the
  rest of this repo's Arduino/glue layer).

## Scope notes (milestone 1)

- Receiver only **displays** BPM — no FX/MIDI/CV output yet. That's an
  explicit follow-on once the basic relay is proven on hardware.
- No ack/retry, no LoRaWAN, no mesh — direct point-to-point only.
