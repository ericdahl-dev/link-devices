# 11. Firmware owns musical time; applications are control planes

Date: 2026-07-15
Status: accepted

## Context

Musical time — tempo, beat, phase, transport state, quantization, and the timing
of every clock edge — is real-time, safety-critical work. A dropped or mistimed
`0xF8`/`0xFA` is audible on stage. The devices already own all of this in
host-tested pure C (`beat_source.c`, `clock_output.c`, `clock_ticker.c`,
`ks_tick.c`).

The companion surfaces — the iOS app and the on-device web UI — are tempting
places to add "smart" timing (predict the next downbeat, run a local clock, guess
when an armed transport will fire). Every such addition creates a second source of
truth for time that can disagree with the device, over a network with jitter.

The iOS app already draws the boundary correctly and it is worth making a decision
so it stays that way across future clients (desktop, watch, Live Activity):

- No Ableton Link SDK, no local clock timers (`no Timer/CADisplayLink/DispatchSourceTimer`
  in `Sources/`).
- Transport does not predict the armed→running transition:
  `DeviceDetailViewModel.swift:219-225` — *"Deliberately does NOT touch `status` —
  the device computes the launch state and reports it on the next poll."*
- Effective tempo shown is the firmware's (`status.bpm`), not computed.
- Every client method maps 1:1 to a firmware route (`KitchenSyncClient.swift` ↔
  `ks_web.cpp:877-892`).

## Decision

**Firmware owns musical time. Applications are control planes: they send *intent*
and render *reported state*, and they own no clock.** Concretely, for every present
and future client:

- Clients issue intent (`/live`, `/save`, `/transport`, `/update`) and read
  telemetry (`/status`, `/config.json`). See
  [`../contracts/firmware-http-contract.md`](../contracts/firmware-http-contract.md).
- Clients **must not** predict quantized transitions, run local beat/phase timers,
  or hold optimistic musical-time state. The device computes launch state, effective
  BPM, and phase health, and reports them every poll.
- Cosmetic animation may free-run at the *reported* cadence (e.g. a beat blink at
  `60/bpm`), but it drives nothing over the wire and is not phase-locked to the
  device's beat origin.

This is the platform-level decision. Client repositories (e.g. `KitchenSync-iOS_App`)
**reference** this ADR rather than forking their own "firmware owns timing"
decision, so there is one source of truth.

## Alternatives considered

- **Optimistic client-side prediction** (guess the armed→running transition, run a
  local clock for smoothness). Rejected: over a jittery LAN it shows outputs as
  running that never started, and creates a second time authority. The device
  already computes the truth cheaply.
- **A bidirectional/real-time client transport** (e.g. the app as a Link peer).
  Rejected for the control plane: it pulls GPLv2 licensing and real-time constraints
  into an app that does not need them. Link peering stays on-device / in the
  quarantined spike.
- **Duplicate the timing rules into each client repo.** Rejected: forks the source
  of truth. Clients reference this ADR and the contract doc.

## Consequences

- Clients stay thin and portable; a new client (desktop, Live Activity) inherits the
  rule and the contract without re-deriving timing.
- The firmware is the only place timing bugs can live, which is where the test
  infrastructure already is (56 Unity suites).
- Some UX latency is accepted by design (an armed transport shows "armed" until the
  device reports "running"). This is correct, not a bug — see the quantized-transport
  invariant in [`../contracts/firmware-http-contract.md`](../contracts/firmware-http-contract.md).
- Related: [ADR-0012](0012-configuration-lifecycle.md) (what a client may change live
  vs via reboot) and [ADR-0010](0010-kitchensync-platform-identity.md).
