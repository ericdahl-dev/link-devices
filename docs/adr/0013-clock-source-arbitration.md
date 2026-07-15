# 13. Clock source arbitration

Date: 2026-07-15
Status: proposed

## Context

The platform acquires musical time from several conceptually distinct sources:
Ableton Link, USB-MIDI clock, MIDI clock-in, an internal free-run generator, and
mic-based Follow Beat. The architecture vision imagines a dedicated subsystem that
selects and transitions between these sources.

What actually exists today is **less than that, and inconsistent across products**:

- **X32Link** picks its source *once at boot* from the persisted `input_source`
  field (`TEMPO_SRC_LINK` / `TEMPO_SRC_MIDI`, `tempo_source.cpp`). It is a static
  switch — no runtime transition, no fallback if the selected source goes silent.
- **KitchenSync (P4)** has a real but narrow arbiter: `master_clock_arbiter`
  (`master_clock.c:31`) chooses **Link vs internal** automatically by peer presence,
  and `beat_source_step` (`beat_source.c`) chooses **phase-locked vs free-run**
  within the Link source. There is **no `input_source` field** on the P4.
- **MIDI clock-in** (`midi_clock_in.c`, P4-011) and **Follow Beat** (`follow_beat.c`,
  P4-020) are **display-only**: their detected BPM appears in `/status` and drives
  nothing (`ks_web.cpp:546,562`). Publishing MIDI-in into Link (P4-011 stage 2) is
  not started.

So there are two clock *bases* that can actually drive output (Link, internal),
chosen automatically on the P4; and two additional tempo *detectors* that are
telemetry only. A unified N-source arbitration subsystem does not exist.

## Decision (proposed)

**Generalize the existing P4 arbiter into one clock-source arbitration module that
admits all sources as first-class, selectable bases, with explicit priority and
transition rules** — while keeping the current automatic Link-vs-internal behavior
as the default policy. Specifically, when built:

- A single arbiter owns *which source drives the beat right now*, subsuming
  `master_clock_arbiter` and generalizing it to admit USB-MIDI clock, MIDI-clock-in,
  and (eventually) Follow Beat as bases, not just detectors.
- Source presence/health and transition policy (priority, hold-off, glitch-free
  handover, seeding a fallback from the last good tempo) live in **pure C**, so they
  are host-tested like the rest of the timing engine.
- Selection remains *policy*, not necessarily a user field: automatic-by-presence is
  the default; an explicit user override is a policy input, not a second mechanism.

This is **Proposed**, not accepted. It should not be built until a product needs to
*follow* an external MIDI or audio clock as the master (i.e. P4-011 stage 2 becomes
real work), because that is the first case that needs more than the current 2-way
arbiter.

## Alternatives considered

- **Leave it as-is (per-product ad hoc).** Fine while only Link/internal drive the
  clock. Fails as soon as a third selectable base is needed, and leaves X32Link's
  static switch and the P4's arbiter as two unrelated mechanisms.
- **A user-facing `input_source` field on every product** (like X32Link's). Rejected
  as the primary design: the P4's automatic-by-presence behavior is better UX (it
  Just Works when a Link peer appears), and a manual field alone can't do glitch-free
  fallback. An override can layer on top.
- **Build the full N-source engine now.** Rejected as premature: MIDI-in and
  Follow-Beat are display-only today, so most of the engine would be speculative. Wait
  for the forcing function.

## Consequences

- Until this is accepted and built, the honest description of the system is *two
  clock bases with an automatic 2-way arbiter on the P4, plus two display-only
  detectors* — documentation must not imply a 4-way arbitration subsystem exists.
- When built, it is the natural home for the "clock inputs remain semantically
  distinct" principle, and it makes MIDI-in / Follow-Beat promotion from detector to
  source a contained change.
- It composes with [ADR-0011](0011-control-plane-boundary.md): the arbiter is
  firmware-owned; a client may *report* the active source and *request* an override,
  but never runs the arbitration itself.
- The pure-C, host-tested constraint ([ADR-0003](0003-firmware-pure-c-glue-split.md),
  [ADR-0009](0009-converge-the-clock-box-on-esp-idf.md)) applies: transition policy is
  logic and belongs in a tested `.c` module.
