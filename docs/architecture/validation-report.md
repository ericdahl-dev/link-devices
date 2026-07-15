# KitchenSync — Architecture Validation Report

**Scope:** validation of the supplied architecture package
(`KitchenSync_Architecture_Vision`, `…_Platform_Architecture_Handoff`,
`…_Repository_Roadmap`, `…_ADR_Drafts`, `…_System_Architecture_Specification`)
against the two implementation repositories:

- `ericdahl-dev/link-devices` (firmware — C / Arduino + ESP-IDF)
- `ericdahl-dev/KitchenSync-iOS_App` (companion app — Swift / SwiftUI)

**Method:** every claim below is graded against code, not against the other
documents. Citations are `path:line` in the two repos at the reviewed commits
(`link-devices` `0351c34`, iOS `6a59f4c`). Grades are **True**, **Partially
true**, **Aspirational**, **Remove**, or **Missing**.

**One-line verdict:** the *direction* the package sets is right and worth
adopting, but the documents describe a smaller, tidier, more finished system
than the one in the repositories. The real system is a **multi-product firmware
family mid-way through a framework convergence**, already governed by a mature
ADR set (0003–0009) the package does not acknowledge. The package should be
re-based onto that reality rather than layered on top of it.

---

## A. Executive summary

| # | Package claim | Grade | One-line reason |
|---|---|---|---|
| 1 | KitchenSync is an embedded synchronization platform, not "a Link device" | **True (already lived)** | The primary product is literally `KitchenSync/`; `AGENTS.md` opens by saying so. |
| 2 | Firmware owns musical time; apps are control planes | **True** | iOS is pure HTTP+Bonjour; never computes beat/phase (`DeviceDetailViewModel.swift:219-225`). |
| 3 | One canonical musical timeline feeds all outputs | **Partially true** | True *within* one device-tick; false *across* the family — the clock writer is implemented **3×**. |
| 4 | Every config field is exactly one of Live-safe / Reboot-required | **Partially true** | Real and enforced, but by a single whitelist (`ks_config_live_safe_copy`), not a two-sided per-field classification. |
| 5 | Clock inputs stay semantically distinct (Link / MIDI / Follow Beat / Internal) | **Partially true** | They are distinct, but only 2 of the 4 can *drive* the clock; MIDI-in and Follow-Beat are display-only. |
| 6 | Clock Source Arbitration is a proposed future subsystem | **Aspirational (but partly exists)** | A real 2-way `master_clock_arbiter` already exists; the 4-way version is future. |
| 7 | Link-compatible support and the Ableton SDK are separate concerns | **True** | Clean-room receive-only parser ships; GPLv2 SDK is quarantined to a spike + host tool. |
| 8 | Shared code lives under `X32Link/` (debt) | **True (and growing)** | 62 symlinks; `KitchenSyncTouch/` alone has 53 (ADR-0009 said 41). |
| 9 | Generic Bonjour discovery (debt) | **Partially true** | Service *type* is generic `_http._tcp`; a `dev=` TXT record now disambiguates (firmware side). |
| 10 | Target repo model: `ks-core`, `ks-devices`, `ks-ios`, `ks-mac`, `ks-simulator`, `ks-protocol`… | **Aspirational (sound direction)** | Correct end-state; must be documented as direction, not near-term work. |
| 11 | ADR "drafts" (identity, control-plane, config, arbitration, repo evolution) | **Partially redundant** | Overlap existing 0003–0009; must *continue* numbering from 0010, not restart. |

**Biggest single correction:** claim 3. The honest description is *"a shared
pure quantization/transform library (`clock_ticker` / `clock_output` / `swing` /
`beat_source`), invoked by three separately-maintained per-firmware writer loops,
each recomputing a beat-position scalar from its own front end."* That triplication
is the platform's central live problem, and `ADR-0009` is already the fix.

---

## B. What is already TRUE

### B1. Platform identity is not aspirational — it is the repository's own framing
The package's headline ("KitchenSync is a platform, Ableton Link is one protocol")
reads as a re-brand to be argued for. In the code it is already the operating
assumption:

- `link-devices/AGENTS.md:5-10` — *"`KitchenSync` is the primary product of this
  repo. The others are related products that share its engine."*
- The primary product directory is literally `KitchenSync/` (ESP32-P4); `X32Link`
  is described as *"one of those related products"* (`AGENTS.md:8-9`).
- Link is implemented as **one** input adapter behind a seam
  (`X32Link/tempo_source.h:9-10`: `TEMPO_SRC_LINK` / `TEMPO_SRC_MIDI`), not as the
  system's spine.

**Implication:** the docs should *report* this identity as established fact and
stop arguing for it. The only place it is under-reinforced is the **repository
name** (`link-devices`) and the shared-library folder name (`X32Link/`) — both
historical, both already flagged in-repo.

### B2. Control-plane boundary — firmware owns time, the app renders and intends
Confirmed on both sides of the wire:

- The iOS app has **zero** Ableton Link SDK code and **no local clock timers**
  (no `Timer`, `CADisplayLink`, or `DispatchSourceTimer` anywhere in `Sources/`).
- It never predicts the quantized transport transition:
  `DeviceDetailViewModel.swift:219-225` — *"Deliberately does NOT touch `status`
  — the device computes the launch state and reports it on the next poll."*
- Effective tempo shown is the firmware's (`status.bpm`), not computed
  (`DeviceDetailViewModel.swift:83-87`).
- Every client method maps 1:1 to a registered firmware route
  (`KitchenSyncClient.swift:18-19` ↔ `ks_web.cpp:877-892`).

This is the strongest, cleanest part of the whole architecture and deserves to
be the anchor ADR (see §F, ADR-0011).

### B3. Pure-C / thin-glue split, host-tested
Already an ADR (`0003`) and load-bearing: 56 Unity suites run on the dev box
(`ADR-0009:90`). All the interesting logic (Link gossip parse, ping/pong
measurement, bar/phase math, tick generation, config) is Arduino-free
(`README.md:40-45`). The package's "pure C timing modules / thin hardware glue"
alignment bullets are **true and pre-existing** — they should cite ADR-0003, not
re-assert.

### B4. Link licensing boundary is real and structurally enforced
- **Ships:** a clean-room, **receive-only** gossip parser
  (`X32Link/link_protocol.c`, `link_listener.cpp`) — grep for any transmit
  primitive returns zero matches; the firmware *cannot* form a session alone.
- **Never ships:** the GPLv2 Ableton SDK, confined to `LinkAudioPoC/` (a spike,
  `.gitignore`'d, fetched + patched at build) and `tools/linkcli/` (a host bench
  peer). `THIRD_PARTY.md` lists **zero** bundled Ableton source.

The package's "Link Audio isolation" alignment bullet is true. This is a business
gate (Ableton commercial license) as much as an engineering one, and the docs
should keep it loud.

### B5. Live/reboot split and reboot-on-save
- `/save` hard-reboots: `ks_web.cpp:657-662` (`esp_restart()`);
  X32Link `web_config.cpp:475-483` (`ESP.restart()`).
- `/live` never reboots and is restricted to a firmware-owned field set
  (`ks_config_live_safe_copy`, `X32Link/ks_config.c:236-255`).

The split is genuine and enforced — see §C4 for the nuance the package gets wrong.

---

## C. What is PARTIALLY true (and must be corrected)

### C1. "One canonical musical timeline feeds all outputs"
**Reality:** there is no canonical *timeline object*. The canonical thing is a
**convention** — a monotonically increasing `double` beat position that feeds the
shared pure `clock_ticker`. It is *recomputed every tick* by **two different,
non-shared functions**:

- `tempo_source_beats_now()` (X32Link, `tempo_source.cpp:111`)
- `beat_source_step().beats` (KitchenSync, `beat_source.c:11`)

and the compute→quantize→emit **writer loop is triplicated**:
`X32Link/midi_clock_out_io.cpp`, `KitchenSyncTouch/ktouch_midi_out.cpp`, and
`KitchenSync/main/ks_tick.c` (+ `ks_main.c`). "One timeline feeds all outputs" is
true only in the narrow sense that *within a single device tick* one `bs.beats`
fans out to every output (`ks_tick.c:74-79`).

This is not a nitpick: the triplication is what produced the marquee bugs of the
last cycle (the `0xFA`-after-`0xF8` transport bug fixed on P4 but left in the
Touch — `ADR-0009:20-29`). **`ADR-0009` is the accepted fix and is not yet
executed.** The docs must describe the *current* triplicated reality and point at
0009 as the convergence in flight — not present a single timeline as done.

### C2. The timing pipeline diagram
The package's pipeline —
`Protocol Observations → Clock Source Arbitration → Canonical Beat Timeline →
Output Transformations → Hardware Renderers` — is a reasonable *idealization*, but
maps onto the code unevenly:

| Stage | Reality |
|---|---|
| Protocol Observations | ✅ Real, but **two** separate front ends (Link listener+measurement; MIDI). |
| Clock Source Arbitration | ⚠️ Partial: X32Link = static boot switch; P4 = real 2-way Link⇄internal arbiter (`master_clock.c:31`); **not** a 4-way engine. |
| Canonical Beat Timeline | ❌ Not an object; a recomputed scalar, two implementations, triplicated writer. |
| Output Transformations | ✅ Real (`swing.c`, phase nudge, PPQN division, `bar_reset`) — but only fully wired on P4; X32Link/Touch pass identity params (24 PPQN, 0, 0). |
| Hardware Renderers | ✅ Real: DIN, USB-MIDI, metronome audio, WS2812, GPIO strobe (per firmware). |

Keep the diagram — it is a good *target* mental model — but annotate each box
with "today vs target," and note the two front ends and the per-output transforms
being P4-only.

### C3. Clock inputs "remain semantically distinct"
True that Link / MIDI-clock / Follow-Beat / Internal are distinct concepts. But
the package implies four *selectable clock sources*. In the firmware:

- **X32Link:** 2 sources (Link, USB-MIDI), mutually exclusive, chosen once at
  boot from NVS `input_source`; no runtime switch, no fallback.
- **KitchenSync P4:** **no source field at all.** Two things can drive the clock —
  Link and the internal free-run tempo — chosen *automatically* by peer presence
  (`master_clock_arbiter`, `ks_main.c:240`). **MIDI clock-in** (`midi_clock_in.c`,
  P4-011) and **Follow Beat** (`follow_beat.c`, P4-020) are **display-only**: their
  BPM appears in `/status` and drives nothing (`ks_web.cpp:546,562`).

So "four distinct sources feeding one arbiter" overstates the wiring. Correct
framing: *two* real clock bases (Link, Internal) with an automatic arbiter on the
P4, plus two tempo **detectors** that are currently telemetry only and are the
natural inputs to a future generalized arbiter.

### C4. "Every field is exactly one of Live-safe / Reboot-required"
The split is real and enforced, but **not** as a two-sided, per-field
classification in the firmware. It is one whitelist function
(`ks_config_live_safe_copy`, `ks_config.c:236-255`, "the single owner of the
live-safe field set") plus its *implicit complement*: any field the whitelist
omits is reboot-only because `/save`'s full overwrite is the only path that
writes it. There is no firmware-side "reboot-required" list and no per-field tag.

Interestingly, the **explicit** two-sided invariant the package describes actually
lives in the **iOS app**, not the firmware: `KsLiveEdit` (closed case set) +
`KsConfig.rebootRequiredFormKeys` + `LiveRebootPartitionTests` assert "exactly
one, never both, never neither" (`LiveRebootPartitionTests.swift:62-83`). This is
a genuinely elegant division of labor worth naming in the ADR: **firmware enforces
live-safety by construction; the app makes the full partition explicit and
testable.**

### C5. "Generic Bonjour discovery" debt
Half-corrected already. The service *type* is generic (`_http._tcp`, port 80 —
`wifi_link.c:144`, `X32Link.ino:342`), which is the real false-positive surface.
But a distinguishing **TXT record** now exists on the firmware side
(`dev=kitchensync|x32link`, `model`, `target`, `fw` — `wifi_link.c:138-143`,
`X32Link.ino:355-362`, ESP-031/ESP-037), and the iOS `DeviceMatch` already prefers
TXT when present and falls back to hostname prefix (`DeviceMatch.swift:22-46`).
**Cross-repo drift to flag:** the iOS code comments still say TXT is *"`nil` for
every unit in the field today"* (`KitchenSyncDiscovery.swift:88-91`) while the
firmware now emits it — the app is ready but its docs/telemetry assumption lags
the firmware. This is exactly the kind of contract skew the initiative should fix.

---

## D. What is ASPIRATIONAL (correctly future — keep, but label)

- **`ks-core` / `ks-protocol` / `ks-simulator` / `ks-mac` extraction and the
  `ks-*` rename.** None exist. The direction is sound (see §G) but must be
  documented as *direction with triggers*, not backlog. The package already says
  "document, don't implement" — good; the docs must hold that line and resist
  creating empty abstractions.
- **4-way Clock Source Arbitration subsystem.** The 2-way P4 arbiter is the seed;
  generalizing it to admit MIDI-clock-in and Follow-Beat as *selectable* bases
  (P4-011 stage 2 — "publish MIDI-in into Link" — is *not started*) is real future
  work. Keep as **Proposed** (ADR-0013).
- **Desktop app (`ks-mac`) and future hardware.** Nothing in-repo. Pure direction.
- **Simulator.** Note that a partial answer already exists: `X32_emulator/` is an
  on-device X32 OSC console emulator used in integration tests, and
  `tools/linkcli` is a host Link peer. A future `ks-simulator` should be framed as
  *generalizing these*, not greenfield.

---

## E. What should be REMOVED or reframed in the package

1. **The unnumbered ADR "drafts" as-is.** They restart the conversation the repo
   already had. `ADR-0004` (touch-vs-web config), `0003` (pure-C split), `0005/0009`
   (per-target framework / convergence) already exist and are richer. Fold the
   drafts into **new** ADRs 0010+ that *reference* the existing set. Do **not**
   create `iOS/docs/decisions/0001-firmware-owns-timing.md` as proposed — it
   restarts numbering in a second repo and forks the source of truth. "Firmware
   owns timing" is a **platform** decision; it belongs in `link-devices` ADR-0011
   and should be *referenced* from the iOS docs.
2. **The clean `ks-devices` singular box.** It hides that there are 4+ firmware
   products with different chips, frameworks, and release cadences. Either show the
   family inside `ks-devices` or keep per-product boundaries visible.
3. **"Firmware owns … quantization, source selection" stated flatly.** True for
   quantization; "source selection" is P4-automatic / X32Link-manual and worth the
   nuance rather than a blanket claim.
4. **Any implication that the timeline/writer is already unified.** Remove; replace
   with "converging under ADR-0009."

---

## F. What important architecture is MISSING from the package

The package omits several concepts that are load-bearing in the actual system:

1. **Framework convergence (Arduino ⇄ ESP-IDF) — the single biggest active
   architectural thread.** `ADR-0005` → `ADR-0009`: the P4 is ESP-IDF, the S3
   products are arduino-cli, and the Touch is migrating to ESP-IDF to kill the
   duplicate clock writer. The package says nothing about frameworks. This *is* the
   near-term architecture story and must be central.
2. **The shared-library-inside-a-product problem** (`X32Link/` is both the mixer
   product *and* the pure-C home). This is the mechanical root of the naming debt
   *and* of real defects (a bench rig built into the mixer firmware —
   `ADR-0009:41-45`). The `ks-core` extraction is the fix; the docs should connect
   them explicitly.
3. **Host-testability as the real justification for pure C.** The package frames
   pure C as "portable / shared logic." `ADR-0009:82-90` corrects this: the durable
   reason is 56 host-run Unity suites, not framework portability. This matters
   because it survives the single-framework convergence.
4. **The firmware↔app protocol contract as a first-class artifact.** The 6-route
   HTTP + Bonjour surface (`/status`, `/config.json`, `/live`, `/save`,
   `/transport`, `/update`) is the actual cross-repo API. The package mentions
   "docs/contracts/" but never specifies it. It should be the shared source of
   truth both repos point at (see deliverable `firmware-http-contract.md`).
5. **OTA and versioning as architecture.** Dual-slot OTA over `/update`, one shared
   `fw_version.h`, `v<FW_VERSION>` tags, `/status.fw` for fleet audit — this is how
   the platform is actually operated and evolved. Absent from the package.
6. **Quantized transport as a cross-repo contract.** "Play arms on the next bar,
   Stop is immediate; never predict the transition client-side" is enforced in both
   firmware (`TransportLaunchState`) and app. It is a genuine architectural
   invariant, not a UI detail.
7. **The safety rationale behind live/reboot.** The reason the split matters is that
   `/save` drops the device out of the Link session mid-set. That "never reboot on
   stage" constraint is the *why*; the package states the rule without the stakes.

---

## G. Recommended ADR numbering (continue, do not restart)

Existing: `0003`–`0009` (with a real amendment/supersession chain). New ADRs
authored by this initiative:

| ADR | Title | Status | Notes |
|---|---|---|---|
| 0010 | KitchenSync platform identity | Accepted | Records already-lived reality; supersedes the "rebrand" framing. |
| 0011 | Control-plane boundary (firmware owns time) | Accepted | Anchor decision; referenced by the iOS repo instead of a forked 0001. |
| 0012 | Configuration lifecycle: live-safe vs reboot-required | Accepted | Documents the whitelist mechanism + the app's explicit partition. |
| 0013 | Clock source arbitration | **Proposed** | Generalize the 2-way P4 arbiter to N sources; admit MIDI-in / Follow-Beat. |
| 0014 | Repository evolution toward `ks-*` | Accepted (direction) | Direction + extraction triggers; explicitly not near-term work. |

---

## H. Net recommendation

Adopt the package's **direction** wholesale — the platform framing, the
control-plane boundary, the `ks-*` end-state — but **re-base the documentation on
the repository's own mature record** (ADRs 0003–0009, `AGENTS.md`, the P4
`README`) rather than treating the package as the starting point. Three things the
docs must not soften:

1. The clock writer is **triplicated today**; ADR-0009 is the convergence in
   flight. Don't describe a unified timeline as done.
2. Clock-source **arbitration is 2-way and partial**; the 4-way subsystem is
   future.
3. The `ks-*` split is **direction, not work** — and the strongest near-term move
   inside it (`ks-core` extraction) is gated on the Touch→ESP-IDF migration
   (ADR-0009), not on documentation.

The system is in better architectural shape than the package implies — it just
needs its documentation to catch up to its own decisions and to stop describing
the finish line as the current position.
