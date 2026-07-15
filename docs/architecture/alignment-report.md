# KitchenSync — Architecture Alignment Report

> **Status:** point-in-time snapshot (2026-07) of how the implementation lines up
> with the target architecture. Three sections: **already aligned**, **technical
> debt**, **future evolution**. Every item is backed by a code reference. This is a
> companion to [`system-overview.md`](system-overview.md); durable decisions live
> in [`../adr/`](../adr/).

## 1. Already aligned

Things the implementation already does correctly, matching the platform
architecture:

| Aligned | Evidence |
|---|---|
| Pure-C timing/logic modules, host-tested | 56 Unity suites; `test/Makefile`; [ADR-0003](../adr/0003-firmware-pure-c-glue-split.md). |
| Thin hardware glue, per framework | `*_io.cpp` / glue tasks; [ADR-0005](../adr/0005-per-target-firmware-framework.md). |
| Firmware owns transport / beat / phase / quantization | `beat_source.c`, `clock_output.c`, `ks_tick.c`; app owns none of it. |
| Control plane is pure HTTP + Bonjour, no Link SDK | iOS `KitchenSyncClient.swift`; zero Ableton deps; [ADR-0011](../adr/0011-control-plane-boundary.md). |
| Live-safe vs reboot-required split, enforced | `ks_config_live_safe_copy` (`ks_config.c:236`); `/save`→`esp_restart()` (`ks_web.cpp:661`); iOS partition test. |
| Quantized transport as a reported (not predicted) state | `TransportLaunchState`; iOS renders `/status.launch[N]`. |
| Link Audio / GPLv2 isolation | `LinkAudioPoC/` quarantined, SDK `.gitignore`'d; `THIRD_PARTY.md` bundles no Ableton source. |
| Clean-room, receive-only Link parser ships | `X32Link/link_protocol.c` + `link_listener.cpp`; no transmit primitives. |
| Device identity via TXT record | `dev`/`model`/`target`/`fw` (`wifi_link.c:138`); iOS `DeviceMatch` prefers TXT. |
| One shared version identity + OTA | `fw_version.h`, `v<FW_VERSION>` tags, `/status.fw`, dual-slot `/update`. |
| A single owner for the config-page look | `ui_chrome.{h,c}`; [ADR-0008](../adr/0008-p4-web-ui-is-the-brand-reference.md). |

## 2. Technical debt

Architectural mismatches, historical naming, and cross-repo coupling that should
eventually change. Ordered roughly by leverage.

### 2.1 The clock writer is triplicated (highest leverage)
The compute→quantize→emit writer loop exists three times: `X32Link/midi_clock_out_io.cpp`,
`KitchenSyncTouch/ktouch_midi_out.cpp`, `KitchenSync/main/ks_tick.c` (+ `ks_main.c`).
A fix in one silently leaves the others — this produced the `0xFA`-after-`0xF8`
transport bug fixed on the P4 but shipped on the Touch.
- **Root cause:** two glue frameworks straddling the *same* product.
- **Fix in flight:** [ADR-0009](../adr/0009-converge-the-clock-box-on-esp-idf.md)
  (Touch → ESP-IDF). Accepted, **not yet executed**.

### 2.2 Shared library lives inside a product (`X32Link/`)
`X32Link/` is simultaneously the mixer product *and* the home of all shared pure C
(including `ks_config.c`, which the P4 depends on). Consequences:
- "Put the shared module in X32Link" slides into "put the feature in X32Link" — a
  bench rig was once built into the mixer firmware and had to be backed out
  ([ADR-0009 §Context](../adr/0009-converge-the-clock-box-on-esp-idf.md)).
- **A 62-symlink farm:** `KitchenSyncTouch/` (53), `LoraLink/` (8), `X32_emulator/`
  (1) symlink files out of `X32Link/`. The count has grown since ADR-0009 recorded
  41 for the Touch — the debt is accreting.
- **Fix:** `ks-core` extraction, gated on 2.1. See
  [ADR-0006/0007/0009](../adr/) and [repository-roadmap.md](repository-roadmap.md) step 2.

### 2.3 Contract mirrored by hand across repos
The firmware↔app contract is duplicated: Swift types mirror C field grammars by
hand, kept honest by convention + a partition test. Small enough today to be cheap,
but a standing drift risk — the `dev` TXT record skew (firmware emits it; iOS
comments assume it absent) is a live instance.
- **Fix (only when it pays):** `ks-protocol` extraction —
  [repository-roadmap.md](repository-roadmap.md) step 4. Not yet justified.

### 2.4 Historical naming
- `link-devices` — the repo name predates the platform framing and undersells it.
- `X32Link/` — names a related product but holds the shared engine.
- These are **cosmetic**; they should not front-run 2.1/2.2. Rename is
  [repository-roadmap.md](repository-roadmap.md) step 3.

### 2.5 Generic Bonjour service type
The `_http._tcp` service *type* is generic; only the TXT `dev` key disambiguates. A
client that filters on type alone false-positives on any LAN web server. Mitigated,
not eliminated, by the TXT record. A dedicated subtype would make discovery exact.

### 2.6 Partial output-transform wiring
Swing / phase nudge / per-output division are real but only fully wired on the P4;
X32Link/Touch pass identity parameters. Not a defect (those products don't need
them yet), but the pipeline is not uniform across the family.

## 3. Future evolution

Directional moves, each with the condition that should trigger it. Full detail in
[repository-roadmap.md](repository-roadmap.md) and [ADR-0014](../adr/0014-repository-evolution.md).

| Move | Why | Trigger |
|---|---|---|
| **Framework convergence** (Touch → ESP-IDF) | Kills the triplicated writer; unblocks `ks-core`. | Already fired (shipped duplicate-writer bugs). In flight (ADR-0009). |
| **`ks-core` extraction** | Give the shared engine an identity independent of any product; end the X32Link conflation. | Convergence complete **and** a second consumer (simulator/desktop) or another conflation defect. |
| **`ks-protocol` extraction** | One versioned schema for firmware + all clients. | The contract churns enough that hand-mirroring is a recurring drift source. |
| **`ks-simulator`** | Deterministic integration tests without hardware. | CI/test needs device behavior HIL can't give; generalize `X32_emulator` + `linkcli`. |
| **Clock-source arbitration (4-way)** | Admit MIDI-clock-in and Follow-Beat as *selectable* bases, not just detectors. | A product needs to follow an external MIDI/audio clock as the master (P4-011 stage 2). [ADR-0013](../adr/0013-clock-source-arbitration.md). |
| **Repo renames → `ks-devices` / `ks-ios`** | Names match the platform. | Historical names actively confuse contributors/users. Lowest urgency. |
| **`ks-mac` / desktop, future hardware** | New control planes / tiers. | A concrete user need. No code today. |

**The through-line:** every future step is gated on a real pressure, and the
structural steps (convergence → `ks-core`) come before the cosmetic ones (renames).
Do not create empty `ks-*` repositories ahead of their triggers.
