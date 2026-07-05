# 5. Per-target firmware framework: Arduino for the S3, ESP-IDF for the P4

Date: 2026-07-05
Status: accepted

## Context

The product line now spans two SoCs. The shipped **ESP32-S3 `X32Link`** is an
Arduino sketch (arduino-cli, `adafruit_qtpy_esp32s3` FQBN) — WiFiUDP for Link, a
LovyanGFX touch LCD, a web config server, TinyUSB-device MIDI. The new **ESP32-P4
`P4Hub`** hub tier (P4-001..006) grows I/O the S3 physically can't: USB-MIDI
**host**, WiFi via the onboard **ESP32-C6** co-processor, later audio.

The 2026-07-04 hardware spikes established a hard constraint: the P4 hub features
require ESP-IDF components that the Arduino-ESP32 core does not cleanly expose —
`usb_host` (USB-MIDI host, P4-003/005) and `esp_hosted` / `esp_wifi_remote`
(hosted WiFi through the C6, P4-004). All three P4 spikes (USB host, WiFi, Link
listener) were ESP-IDF and worked; the equivalent under Arduino would fight the
framework.

This raised the question: unify on one framework — in particular, rewrite the
working S3 `X32Link` in ESP-IDF to match?

## Decision

**Choose the firmware framework per target, and keep the pure logic shared:**

- **S3 `X32Link` stays Arduino.** It is shipped and hardware-validated; its glue
  leans on Arduino-native libraries (LovyanGFX, the web server, TinyUSB-device).
  There is no user-facing gain in rewriting it, and its limits (no USB host, no
  second radio) are *hardware* — ESP-IDF would not unlock them.
- **P4 `P4Hub` is a native ESP-IDF app.** Its headline features need ESP-IDF
  components, so this is not a preference but a requirement.
- **The pure, host-tested C modules are shared unchanged across both**, compiled
  into each app by relative-path `SRCS` (single source, no copies/forks) and
  still covered by their Unity suites in `test/`. P4Hub's scaffold already
  compiles `X32Link/clock_ticker.c` as-is.

This does **not** weaken [ADR-0003](0003-firmware-pure-c-glue-split.md): that ADR
already says the interesting logic is pure C and only the glue is platform
specific. Two glue frameworks is exactly the shape ADR-0003 anticipates — the
seam that matters (the pure modules) is unchanged.

## Consequences

- Two build systems (arduino-cli for S3, `idf.py` for P4) and two CI compile
  jobs. This is a low, one-time tax because the shared surface is the pure C, not
  the glue.
- New *logic* must stay in the pure `.c` layer so both targets keep reusing it;
  resist the temptation to write feature logic inside ESP-IDF or Arduino glue.
- If a pure module ever needs an on-device guard, keep the existing
  `#if defined(ARDUINO) || defined(ESP_PLATFORM)` pattern so it stays host-testable
  and compiles under both frameworks.
- Revisit only if a future S3 feature genuinely needs an ESP-IDF-only capability
  that its hardware can actually support, or if maintaining two glue layers
  becomes a real burden (not expected — glue is thin by ADR-0003).
