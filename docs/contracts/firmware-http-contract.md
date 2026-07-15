# Firmware ↔ Client Contract (HTTP + Bonjour)

> **Status:** source of truth for the wire contract between a KitchenSync device
> and any control-plane client (the iOS app, the on-device web UI, `curl`, a future
> desktop app). Both repositories point here. The **firmware is authoritative**:
> where a client and this document disagree with the firmware source, the firmware
> is right. Exact field grammars live in the cited source files so they cannot rot
> in prose.

## Where the contract is defined (firmware)

| Concern | File |
|---|---|
| Routes + handlers | [`KitchenSync/main/ks_web.cpp`](../../KitchenSync/main/ks_web.cpp) (`ks_web_start`, `:877-892`) |
| Form field grammar | [`X32Link/ks_config.c`](../../X32Link/ks_config.c) (`ks_form_apply`, `ks_config_live_safe_copy`) |
| `/config.json` shape | `KitchenSync/main/` config-json path (`config_json_handler`) |
| `/status` shape | `ks_status.c` (`ks_status_json`) |
| Bonjour / mDNS | [`KitchenSync/main/wifi_link.c`](../../KitchenSync/main/wifi_link.c); X32Link in `X32Link.ino` |

## Discovery (Bonjour / mDNS)

- Devices advertise a **generic** service: `_http._tcp` on port 80. The service
  *type* carries no product identity — any HTTP server on the LAN shares it.
- Identity is carried in a **TXT record** (ESP-031 / ESP-037):
  `dev`, `model`, `target`, `fw`.
  - `dev=kitchensync` — the P4 hub and the Touch (Touch sets `model=touch`).
  - `dev=x32link` — the mixer product (published so the KitchenSync app, which
    matches `dev=kitchensync`, does **not** adopt it).
- Hostnames: `kitchensync-XXXX.local` / `kstouch-XXXX.local` / `x32link-XXXX.local`
  (last two MAC bytes), plus a delegated `kitchensync.local` alias on the P4.

**Client rule:** match on the `dev` TXT key when present; fall back to a hostname
prefix only when TXT is absent. Hostname-prefix matching alone can false-positive
on a stranger's device at a venue — the TXT record exists precisely to make the
match exact. (See the iOS `DeviceMatch` implementation for the reference client.)

> **Known cross-repo skew (2026-07):** the firmware emits the `dev` TXT record, but
> some iOS code comments still assume it is absent in the field. Treat the TXT
> record as present on current firmware; the hostname fallback is for older units.

## Routes

Six machine-facing routes. (`GET /` serves the human web UI and is not part of the
machine contract; clients use `/config.json` instead of scraping HTML.)

| Route | Method | Purpose | Reboots? |
|---|---|---|---|
| `/status` | GET | Live telemetry — BPM, phase health, peers, per-output launch state, transport, detected MIDI-in / Follow-Beat BPM, `fw`, battery. **Poll it (~1 Hz); never cache it.** | no |
| `/config.json` | GET | Current persisted settings, as a machine object (P4-041). 404s on firmware older than 2026-07-14. | no |
| `/live` | POST | Partial patch of **live-safe fields only**; applied instantly. Fields outside the live-safe set are silently ignored. | **no** |
| `/save` | POST | Full config form; validated, persisted to NVS, then the device **reboots**. | **yes** |
| `/transport` | POST | `?out=<N>\|all&play=1\|0` — quantized Start/Stop. Play *arms*; Stop is immediate. | no |
| `/update` | POST | Raw `.bin` body (not multipart), dual-slot OTA. | reboots into new slot |

## Invariants (these are the contract, not the field list)

### 1. Firmware owns musical time
Clients send **intent** and render **reported state**. A client must not compute or
predict tempo, beat, phase, or the transport transition. See
[ADR-0011](../adr/0011-control-plane-boundary.md).

### 2. Live-safe vs reboot-required
Every writable field is, effectively, exactly one of:
- **Live-safe** — accepted by `/live`, applied with no reboot. The set is owned by
  `ks_config_live_safe_copy` in firmware (a whitelist; anything absent is
  reboot-only).
- **Reboot-required** — only `/save` writes it, and `/save` reboots, dropping the
  device out of the Link session for several seconds.

The distinction is **not guessable from field names** (e.g. metronome *volume* is
live, metronome *enable* is not; `led` enable is live, `follow_beat` enable is not).
Clients must classify every field explicitly and keep the two sets disjoint and
exhaustive. The iOS app enforces this with a partition test; adopt the same
discipline in any new client. See [ADR-0012](../adr/0012-configuration-lifecycle.md).

### 3. Quantized transport
`/transport` returns immediately; the device computes the launch state
(stopped / armed / running) and reports it in `/status` on the next poll. **Never
predict the armed→running transition client-side** — a Play arms and fires on the
next bar line, and guessing it shows a running output that never started.

### 4. Passwords are write-only
`/config.json` never returns a WiFi password; a blank password field means "keep
current." WiFi credential edits are keyed by slot **id**, never array position.

### 5. Transport / clock require a real Link session
The firmware's Link stack is receive-only. A device alone on the network reports
`peers:0` and silently drops transport intent. Testing any tempo/transport
behavior requires a transmitting peer (`tools/linkcli`) or a real Link app on the
LAN.

## Client reference implementations

- **iOS:** `KitchenSyncClient.swift` (6 methods ↔ these 6 routes),
  `KitchenSyncDiscovery.swift` + `DeviceMatch.swift` (Bonjour), and
  `LiveRebootPartitionTests.swift` (the partition invariant as a test).
- **On-device web UI:** `ks_web.cpp` + `ui_chrome.{h,c}` (the brand reference page,
  [ADR-0008](../adr/0008-p4-web-ui-is-the-brand-reference.md)).
