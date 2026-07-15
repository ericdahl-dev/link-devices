# KitchenSync — System Architecture Overview

> **Status:** living document. Source of truth for *what the platform is today*
> and where it is going. It was produced by validating an architecture proposal
> against the code — that validation is preserved in
> [`validation-report.md`](validation-report.md). Decisions are recorded as ADRs in
> [`../adr/`](../adr/); this overview narrates and links them rather than
> restating them.

## 1. What KitchenSync is

KitchenSync is an **embedded musical-time synchronization platform**. A family of
ESP32 firmware products take a musical tempo and beat from a source (an Ableton
Link session, USB-MIDI clock, or an internal generator) and render it as
phase-accurate clock, transport, and metronome signals for real hardware — MIDI
(USB and DIN), analog/Eurorack pulses, audible click, and visual metronome.

**Ableton Link is one supported input protocol, not the identity of the system.**
The firmware speaks Link through a small clean-room, receive-only parser behind a
source-selection seam ([`X32Link/tempo_source.h`](../../X32Link/tempo_source.h));
it is not built *on* Link. This distinction is load-bearing for licensing (§6) and
for the roadmap (§7).

This is not a target state — it is how the repository is already organized:
`AGENTS.md` opens by naming `KitchenSync/` (the ESP32-P4 hub) as the primary
product and the others as *related* products that share its engine.

## 2. Product family

Every board is an ESP32 variant. "Arduino" vs "ESP-IDF" below refers to the
**build framework**, not the chip (see [ADR-0005](../adr/0005-per-target-firmware-framework.md)).

| Product | Role | Chip | Framework |
|---|---|---|---|
| **`KitchenSync/`** | **Primary product** — the "pro hub": Link in → USB-MIDI host clock + transport out, 4 configurable outputs (division / phase / swing), metronome, WS2812, live web config, OTA. | ESP32-P4 | ESP-IDF |
| `KitchenSyncTouch/` | The same clock engine with a touch LCD; also the ESP-025 bench rig. **Migrating to ESP-IDF** ([ADR-0009](../adr/0009-converge-the-clock-box-on-esp-idf.md)). | ESP32-S3 / ESP32 | Arduino → ESP-IDF |
| `X32Link/` | Related product: tempo → XR18/X32 mixer FX-delay sync over OSC. **Also the shared-pure-C home** (see §5). | ESP32-S3 | Arduino |
| `LoraLink/` | Related product: LoRa tempo relay (BPM only, no phase) beyond WiFi range. | ESP32-S3 | Arduino |
| `X32_emulator/` | On-device X32 OSC console emulator, used by integration tests. | ESP32-S3 | Arduino |
| `LinkAudioPoC/` | Feasibility spike (not a product). Vendors the **GPLv2** Ableton Link SDK — quarantined (§6). | ESP32-P4 | ESP-IDF |
| `tools/linkcli/` | Host-side Ableton Link **peer** — required to test any device (the firmware cannot form a session alone). | host | — |

## 3. Layered architecture

Two layers, one boundary. See [ADR-0003](../adr/0003-firmware-pure-c-glue-split.md).

```
┌───────────────────────────────────────────────────────────┐
│  Control plane (off-device)                               │
│  KitchenSync-iOS_App, the on-device web UI                │
│  → sends intent, renders reported state, owns no time     │
└───────────────────────────────────────────────────────────┘
        │  HTTP + Bonjour  (see docs/contracts/firmware-http-contract.md)
        ▼
┌───────────────────────────────────────────────────────────┐
│  Firmware (on-device) — owns musical time                 │
│                                                           │
│   thin glue (per framework):  WiFi/UDP, USB, NVS, I2S,    │
│      UART, RMT, LovyanGFX, httpd                          │
│   ───────────────────────────────────────────────         │
│   pure C (host-tested, shared): Link parse, measurement,  │
│      beat_source, clock_ticker, clock_output, swing, bar, │
│      config model, OSC build                              │
└───────────────────────────────────────────────────────────┘
```

- **Pure C** holds all the interesting logic and is host-tested with Unity (56
  suites). Its durable justification is *testability*, not framework portability
  ([ADR-0009](../adr/0009-converge-the-clock-box-on-esp-idf.md) §Decision).
- **Thin glue** is the only per-framework code. There are two glue frameworks
  today (arduino-cli, ESP-IDF) and a convergence in flight (ADR-0009).

## 4. Timing pipeline (today vs target)

The intended mental model:

```
Protocol observations → source arbitration → canonical beat → output transforms → renderers
```

What the code actually does today, per firmware:

**KitchenSync (P4)** — the most complete pipeline:

```
WiFi(C6) ─ link_protocol ─► LinkTimeline ┐
link_measure_io ─ link_measurement ─► GhostXForm ┤
                                                  ▼
              master_clock_arbiter  (Link vs internal, by peer presence)
                                                  │
                                     beat_source_step  (phase-locked vs free-run)
                                                  │  bs.beats  (a double)
                    ┌─────────────────────────────┼──────────────────────────┐
                    ▼                             ▼                           ▼
       per output ×4: clock_output          transport (Start/Stop)      metronome
       swing_warp → phase nudge →           → usb_midi_pack             → metronome_audio
       clock_ticker(ppqn)                        │                      (ES8311)
                    └── usb_midi_pack / DIN / RMT strobe / WS2812 ◄──────┘

  MIDI clock IN ─ midi_clock_in ─ midi_bpm_calc ─► /status (display only)
  Follow Beat   ─ follow_beat (mic autocorrelation) ─► /status (display only)
```

Key facts, and where the model and the code diverge:

- **The "canonical beat" is a recomputed `double`, not a shared timeline object.**
  Within one device tick a single `bs.beats` value fans out to every output
  ([`KitchenSync/main/ks_tick.c`](../../KitchenSync/main/ks_tick.c) `ks_tick_step`).
  Across the family it is **recomputed by two implementations**
  (`tempo_source_beats_now` on X32Link, `beat_source_step` on the P4) and the
  writer loop is **implemented three times** (X32Link, KitchenSyncTouch,
  KitchenSync). Unifying this is exactly [ADR-0009](../adr/0009-converge-the-clock-box-on-esp-idf.md).
- **Source arbitration is 2-way and partial.** The P4 `master_clock_arbiter`
  chooses Link-vs-internal automatically by peer presence; X32Link chooses
  Link-vs-USB-MIDI once at boot from the persisted `input_source` field. Neither is
  the 4-way engine implied by "Link / MIDI / Follow / Internal" — MIDI-clock-in and
  Follow-Beat are **detectors surfaced on `/status`, not clock inputs**. Generalizing
  the arbiter is [ADR-0013 (Proposed)](../adr/0013-clock-source-arbitration.md).
- **Output transforms** (swing, phase nudge, PPQN division, bar-reset pulse) are
  real pure modules but only fully wired on the P4; X32Link/Touch pass identity
  parameters (24 PPQN, no swing/phase).

## 5. The control-plane boundary

Firmware owns tempo, beat, phase, transport, scheduling, quantization, and (where
automatic) source selection. Control planes — the iOS app and the on-device web
UI — **send intent and render reported state; they own no musical time.** See
[ADR-0011](../adr/0011-control-plane-boundary.md).

The boundary is the HTTP + Bonjour contract documented in
[`../contracts/firmware-http-contract.md`](../contracts/firmware-http-contract.md):
six machine-facing routes (`/status`, `/config.json`, `/live`, `/save`,
`/transport`, `/update`) plus a generic `_http._tcp` advertisement disambiguated by
a `dev=` TXT record. Two invariants that the boundary enforces:

- **Configuration lifecycle** — [ADR-0012](../adr/0012-configuration-lifecycle.md).
  `/live` applies a whitelisted set of live-safe fields with no reboot; `/save`
  persists the full form and **reboots** (`esp_restart()`), which drops the device
  out of the Link session mid-set. The live-safe set is owned by one function,
  `ks_config_live_safe_copy` ([`X32Link/ks_config.c`](../../X32Link/ks_config.c)).
- **Quantized transport** — Play *arms* and fires on the next bar line; Stop is
  immediate. The device computes launch state and reports it every poll; clients
  must never predict the armed→running transition.

## 6. The Ableton Link boundary (licensing)

Two entirely separate Link implementations, kept apart on purpose:

- **Ships:** `X32Link/link_protocol.c` — a clean-room, **receive-only** gossip
  parser written from the observable wire format. No Ableton source. The device
  joins a session but cannot create one; it never transmits.
- **Never ships:** the real Ableton Link SDK (GPLv2), confined to `LinkAudioPoC/`
  (a spike; the SDK is `.gitignore`'d and fetched at build) and `tools/linkcli/`
  (a host bench peer). `THIRD_PARTY.md` bundles zero Ableton source.

Productizing anything that links the SDK is a **business gate** (Ableton
commercial license), not an engineering one. Keep the quarantine intact.

## 7. Where this is going

The long-term direction is a set of focused repositories
([`repository-roadmap.md`](repository-roadmap.md), [ADR-0014](../adr/0014-repository-evolution.md)):
`ks-core` (the pure timing/protocol library, extracted out of `X32Link/`),
`ks-devices` (the firmware targets), `ks-ios` / `ks-mac` (control planes),
`ks-protocol` (shared schema), `ks-simulator` (generalizing `X32_emulator` +
`linkcli`).

**This is direction, not backlog.** The guiding rule is that extraction happens
only when ownership or release cadence justifies it. The strongest near-term move
inside this direction — extracting `ks-core` — is gated on the Touch→ESP-IDF
convergence (ADR-0009), because that migration is what makes the shared engine
leave `X32Link/` symlink-neutral. Do not create empty `ks-*` repositories ahead of
that.

## 8. See also

- [`repository-roadmap.md`](repository-roadmap.md) — the `ks-*` direction and its triggers.
- [`alignment-report.md`](alignment-report.md) — already-aligned / technical-debt / future-evolution snapshot.
- [`../contracts/firmware-http-contract.md`](../contracts/firmware-http-contract.md) — the firmware↔client API.
- [`../adr/`](../adr/) — the decision record (0003–0014).
- Root [`AGENTS.md`](../../AGENTS.md) — the day-to-day firmware map and gotchas.
