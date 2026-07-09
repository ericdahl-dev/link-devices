# Changelog

Firmware versions come from `X32Link/fw_version.h` (`FW_VERSION`, shared by
every firmware in this repo). Each release is tagged `v<FW_VERSION>` so any
distributed .bin traces back to source.

## [2.2.0] - 2026-07-09

### Added
- Firmware version identity (LNK-038): every device now reports what it runs —
  serial boot banner and periodic status line, web UI header/footer, `/status`
  JSON `"fw"` field (fleet-auditable by script), and the `/update` OTA page
  shows the running version before you flash over it. One shared
  `fw_version.h` across X32Link, KitchenSync, and the X32 emulator.
- Beat LED colours (LNK-039): on RGB-LED boards (QT Py NeoPixel) the beat LED
  follows the web UI's BEAT/BAR1 colour pickers — bar-1 downbeat gets the
  accent colour, live-applied with no reboot. The WiFi-down diagnostic blink
  stays fixed green so a dark user colour can never hide it.
- CI now compile-checks the QT Py NeoPixel board profile (`LED_RGB` path)
  alongside the headless and Waveshare touch variants.

### Changed
- Web UI header badge shows the real firmware version (was a hardcoded
  "FW 2.0" placeholder).

### Removed
- X32MidiClock standalone sketch (LNK-024): superseded by X32Link's runtime
  Link/MIDI source selector since LNK-010. Its MIDI modules now live in
  `X32Link/`; the tracked `tests/test_x32_port` build artifact is no longer
  committed.

## [2.1.0] - 2026-07-08

First versioned build (flashed over USB; superseded same-week by 2.2.0, the
first OTA-delivered release). Everything before this shipped untagged.
