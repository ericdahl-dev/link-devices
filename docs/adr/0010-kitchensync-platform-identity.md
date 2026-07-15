# 10. KitchenSync is a synchronization platform; Ableton Link is one input protocol

Date: 2026-07-15
Status: accepted

## Context

The repository is named `link-devices` and its shipped device is `X32Link`, so a
new reader reasonably concludes the project *is* an Ableton Link product. That
framing is now wrong, and the code already contradicts it:

- The primary product is `KitchenSync/` (ESP32-P4), and `AGENTS.md` opens by
  saying so: *"`KitchenSync` is the primary product of this repo. The others are
  related products that share its engine."*
- Ableton Link is implemented as **one input adapter** behind a source-selection
  seam (`X32Link/tempo_source.h`: `TEMPO_SRC_LINK` / `TEMPO_SRC_MIDI`), alongside
  USB-MIDI clock, MIDI-clock-in, an internal generator, and mic-based Follow Beat.
- The system's outputs (USB/DIN MIDI clock + transport, analog pulses, metronome,
  visual metronome) are about *distributing musical time to hardware*, not about
  Link.

The naming and the one-line self-descriptions lag the actual scope. This is a
documentation/identity decision, not a code change.

## Decision

**KitchenSync is the platform and product family. Ableton Link is one supported
synchronization *input*, not the identity of the system.** The system is an
embedded musical-time synchronization platform: it acquires a tempo/beat from a
selectable source and renders it as phase-accurate clock, transport, and
metronome outputs for hardware.

Documentation, READMEs, and future naming should reinforce this consistently. The
repository and folder names (`link-devices`, `X32Link/`) are historical and are
addressed as low-urgency renames in [ADR-0014](0014-repository-evolution.md); this
ADR does not rename anything.

## Alternatives considered

- **Keep the "Link device family" framing.** Rejected: it misdescribes the primary
  product, buries the multi-source design, and implies a dependency on Ableton the
  firmware deliberately does not have (the Link parser is clean-room, receive-only —
  [ADR-0011](0011-control-plane-boundary.md), `THIRD_PARTY.md`).
- **Rename everything now to `ks-*` to force the identity.** Rejected as premature —
  renames are cosmetic and should follow the structural work, not lead it
  (see [ADR-0014](0014-repository-evolution.md)).

## Consequences

- New documentation leads with "synchronization platform," and treats Link as a
  peer of MIDI/internal/Follow-Beat sources, not the spine.
- The Link licensing boundary ([ADR-0011](0011-control-plane-boundary.md) references;
  `LinkAudioPoC` quarantine) becomes easier to explain: Link is one protocol among
  several, and the GPLv2 SDK is isolated from the platform.
- Reviewers should stop treating "this isn't very Link-centric" as drift. Multi-source
  is the design.
- The historical names remain a known, tracked cosmetic debt until
  [ADR-0014](0014-repository-evolution.md)'s rename trigger fires.
