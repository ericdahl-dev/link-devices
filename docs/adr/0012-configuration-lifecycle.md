# 12. Configuration lifecycle: live-safe vs reboot-required

Date: 2026-07-15
Status: accepted

Relates to [ADR-0004](0004-touch-realtime-web-config.md) (which config surface
carries which controls) — this ADR governs the orthogonal question of *when a
change takes effect*.

## Context

A configuration change is one of two very different things:

- A **live-safe** edit — timing (swing, phase nudge, division), tempo, metronome
  volume/voice, LED — can be applied to the running device instantly.
- A **reboot-required** change — WiFi credentials, the metronome/Follow-Beat/codec
  *enable* flags — needs a reconnect or a hardware re-init, so it only takes effect
  after a restart.

The stakes are real: `/save` reboots the device (`ks_web.cpp:661`, `esp_restart()`),
which drops it out of the Link session and stops all clock output for several
seconds — destructive mid-set. And the split is **not guessable from field names**:
metronome *volume* is live but metronome *enable* is not; `led` enable is live but
`follow_beat` enable is not. On the device's own web page those toggles are the
same component forty pixels apart.

Today this is implemented, but the mechanism is worth pinning down so it does not
erode and so every client applies it identically.

## Decision

**Every writable configuration field is, effectively, exactly one of live-safe or
reboot-required, and the classification is authoritative in firmware.**

- **Firmware enforces live-safety by construction.** `/live` parses the POST into a
  candidate config, then merges into the running config *exclusively* through
  `ks_config_live_safe_copy` (`X32Link/ks_config.c:236-255`) — "the single owner of
  the live-safe field set." Any field that function omits cannot be changed without
  a reboot: the only other write path is `/save`, which overwrites everything and
  restarts. A client that POSTs a reboot-only field to `/live` has it silently
  dropped. There is no per-field tag; the whitelist *is* the classification, and its
  implicit complement is the reboot-required set.
- **Clients must make the partition explicit and keep it total.** Because firmware
  states live-safety as one whitelist, a client must enumerate both sides and assert
  they are **disjoint and exhaustive** over the save form — every field is
  classified, none is both. The iOS app is the reference: a closed `KsLiveEdit` case
  set (only live-safe fields can be routed to `/live`), an explicit
  `rebootRequiredFormKeys` set, and `LiveRebootPartitionTests` that fails CI if any
  field is unclassified or double-classified.

Net division of labor: **firmware guarantees you cannot live-edit a reboot-only
field; the client guarantees you cannot ship a control whose lifecycle is
unclassified.** Both halves are required.

## Alternatives considered

- **Per-field `live`/`reboot` tags in the firmware config struct.** More explicit,
  but it duplicates state (a tag plus the actual apply logic) that can disagree, and
  it is more code on a constrained device. The whitelist function is already the
  single apply path, so it is the natural single source of truth. A future
  `ks-protocol` schema could carry the classification as data (see
  [ADR-0014](0014-repository-evolution.md)); until then, the whitelist stands.
- **Warn-on-reboot instead of partitioning.** Rejected: a warning label is exactly
  what the device web page effectively has, and it is why the split is dangerous —
  the iOS app fixes it structurally (reboot-only controls are unreachable from the
  live screen) rather than with a warning.
- **Let clients guess from field names.** Rejected: the split is deliberately not
  name-derivable.

## Consequences

- Adding a config field is a two-place change: decide its lifecycle, and add it to
  the firmware whitelist *or* accept it as reboot-only — and add it to the client's
  classification, or the partition test fails.
- If firmware later makes a field live-safe, the client's partition test is what
  surfaces it, instead of a user discovering it on stage.
- The `/live` vs `/save` boundary is part of the wire contract
  ([`../contracts/firmware-http-contract.md`](../contracts/firmware-http-contract.md))
  and applies to every client, not just iOS.
- WiFi passwords are write-only and slot-`id`-keyed (never array position) —
  a reboot-required field with an extra safety rule.
