# Third-party credits

This firmware family stands on other people's work. Thanks to the authors and
communities below. Each component remains under its own license, held by its
respective copyright holders.

## Bundled in this repository

| Component | Author(s) | License | Used by | Location |
|---|---|---|---|---|
| **Unity** (unit-test framework) | Mike Karlesky, Mark VanderVoord & Greg Williams — [ThrowTheSwitch.org](https://www.throwtheswitch.org/unity) | MIT | host test suites (`test/`) | vendored in [`lib/unity/`](lib/unity/) — full text in [`lib/unity/LICENSE.txt`](lib/unity/LICENSE.txt) |

## Pulled at build time (not stored here)

### ESP-IDF managed components — P4Hub (ESP-IDF)

Fetched by the IDF component manager per `P4Hub/main/idf_component.yml`; each is
by **Espressif Systems**, **Apache-2.0**.

| Component | Purpose |
|---|---|
| [`espressif/esp_hosted`](https://components.espressif.com/components/espressif/esp_hosted) | WiFi on the P4 via the onboard ESP32-C6 (ESP-Hosted transport) |
| [`espressif/esp_wifi_remote`](https://components.espressif.com/components/espressif/esp_wifi_remote) | WiFi API surface over the hosted link |
| [`espressif/es8311`](https://components.espressif.com/components/espressif/es8311) | ES8311 codec driver for the onboard-speaker metronome |

### Frameworks & Arduino libraries — X32Link / X32MidiClock (arduino-cli)

| Component | Author(s) | License |
|---|---|---|
| **ESP-IDF** (P4Hub) | Espressif Systems | Apache-2.0 |
| **Arduino-ESP32** core (X32Link, X32MidiClock) | Espressif Systems | LGPL-2.1-or-later |
| **LovyanGFX** — touch-LCD graphics ([`X32Link/touch_display.*`](X32Link/)) | lovyan03 (Kenji Ono) | BSD-2-Clause (FreeBSD) |

### Web fonts — loaded at runtime by the config UI (CDN, not redistributed here)

The rack-panel web UI (`P4Hub/main/p4hub_web.cpp`, `X32Link/web_config.cpp`)
references these over Google Fonts / jsDelivr. All **SIL Open Font License 1.1**.

| Font | Author | Source |
|---|---|---|
| **Bricolage Grotesque** | Mathieu Triay | Google Fonts |
| **DM Mono** | Colophon Foundry (for Google) | Google Fonts |
| **DSEG** (7-seg display, v0.46.0) | Keshikan | [github.com/keshikan/DSEG](https://github.com/keshikan/DSEG) via jsDelivr |

## Protocols (independent implementations — no third-party code)

- **Ableton Link** — the Link session protocol is by **Ableton AG**. This project
  speaks it via a **clean-room implementation** (gossip parse, ghost-xform
  measurement, timeline/phase math in `link_protocol` / `link_measurement` /
  `link_phase`) written from the observable wire protocol; no Ableton source is
  used or included. "Ableton" and "Link" are trademarks of Ableton AG.
- **MIDI** is a specification of the MIDI Association; the clock/transport bytes
  here are implemented directly.

## Documentation & references

- **X32 / M32 OSC protocol — Patrick Maillot.** The entire X32 / XR18 OSC side of
  this project (writing FX delay times and reading mixer state over OSC) rests on
  **Patrick Maillot's** meticulous *Unofficial X32/M32 OSC protocol* documentation:
  <https://sites.google.com/site/patrickmaillot/x32>. Behringer publishes no
  official OSC spec; his reverse-engineered reference is what makes talking to the
  console possible at all. This work would not exist without it — thank you.

---

*Spot a missing or mis-stated attribution? It's a bug — please open an issue.*
