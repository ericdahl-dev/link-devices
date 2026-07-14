# iOS companion app — planning doc (2026-07-14)

**Status:** planning only — not building yet.

Scope requested: an iOS app that (1) discovers KitchenSync devices on the LAN and
controls/updates their settings, (2) functions as a **software KitchenSync Touch**
(its own Link peer with a transport UI and MIDI clock/transport out), and (3) takes
audio in from an interface plugged into the phone and publishes it as a **Link
Audio** channel.

These three are not one uniform effort. Function 1 is plain HTTP/Bonjour against a
protocol that already exists and is fully specified below. Functions 2 and 3 each
require embedding Ableton's Link SDK in a phone app, which raises the same
licensing question `LinkAudioPoC/README.md` already flagged for the P4 hardware —
and Function 3 is the exact software-side twin of the already-proven **P4-032**
mic→Link-Audio spike. Recommend building and shipping in that order, as three
phases, each independently useful.

---

## Phase 1 — Fleet control app (no licensing gate)

Discover every KitchenSync on the LAN, show live status, edit config, push OTA
updates. This is a pure HTTP client against the protocol `KitchenSync/main/ks_web.cpp`
already serves — nothing here requires Link inside the phone app.

### Discovery

The firmware advertises **generic** `_http._tcp` via mDNS
(`KitchenSync/main/wifi_link.c:125`, `mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0)`)
with hostname `kitchensync-XXXX.local` (last two MAC bytes) plus a delegated
`kitchensync.local` alias (`ESP-012`). There is **no service subtype and no TXT
record** distinguishing it from any other HTTP device on the network (a printer, a
router admin page, an X32Link unit — which answers the same generic `_http._tcp`
under its own hostname prefix).

So the app can't `NWBrowser` for a KitchenSync-specific service type; it has to:

1. Browse `_http._tcp` with `NWBrowser` (Network.framework — `NetServiceBrowser` is
   legacy but works too), resolve each result to host + port.
2. Filter candidates by hostname prefix (`kitchensync-*.local` / `kitchensync.local`).
3. Confirm with `GET /status` and check the response shape — KitchenSync's JSON
   carries fields no other device on this LAN reports (`follow_enabled`, `launch`,
   `link_owns`; see `ks_status.c`). This also doubles as the first live-status fetch,
   so it's not wasted work.

**Firmware gap worth filing (not blocking Phase 1, cheap when it lands):** add a TXT
record (e.g. `model=kitchensync`) or a dedicated service type
(`_kitchensync._tcp`) to `mdns_service_add`, so discovery stops being a heuristic.
One-line firmware change, would retroactively make the app's filtering trivial. File
as a P4 ticket if this plan is greenlit; the app should be written host-prefix-first
either way since existing units in the field won't have new firmware.

### Control surface (verbatim from `ks_web.cpp` / `ks_form.c`)

| Endpoint | Method | Body | Effect |
|---|---|---|---|
| `/status` | GET | — | JSON: `bpm, min (midi bpm), peers, usb, tx, fw, follow_enabled, follow_bpm, follow_confidence, follow_valid, launch[4], playing, link_owns`, plus tick-health (`drop,burst,gap,work,over,core,w_beats,w_clock,reprime`) and phase-health (`xf,xf_step,xf_max,rtt_min,rtt_max`) — see `ks_status.c` for exact keys |
| `/live` | POST | `x-www-form-urlencoded`, **partial patch** | Applies immediately, no reboot: per-output `clk{N}_en/cable/div/nudge`, `clk{N}_follow`, swing, `clock_out`, `metro_accent`, `metro_vol`, `metro_voice`. Omitted keys keep their current value (`ks_form_apply` — no checkbox-clear) |
| `/save` | POST | `x-www-form-urlencoded`, **full form** | Persists to NVS + reboots. Required for WiFi credentials and for turning on a metronome that was off (codec/I2S only come up at boot). Unchecked boxes must still be *sent as absent* — `ks_form_resolve` clears them; the app must replicate "checkbox absent → off," not send `false` |
| `/transport` | POST | query string `?out=N\|all&play=1\|0` | Quantized per-output Start/Stop (ESP-011), armed to the next bar line — mirror this with an "armed" UI state, see below |
| `/update` | POST | raw `.bin` body (no multipart) | OTA into the inactive slot (`P4-017`, dual-slot). One `.bin` per firmware target; do **not** cross-flash an X32Link binary onto a KitchenSync unit |

Build a typed Swift client (`KitchenSyncDevice`) around this table rather than ad hoc
`URLSession` calls scattered through views — one place owns field names, so a
firmware-side field rename is a one-file fix in the app too.

### UI

- **Device list** (mDNS-discovered + manually-added-by-IP fallback for units off the
  local subnet / VPN'd in), each row live-polling `/status` at ~1 Hz for BPM/peers/
  playing.
- **Device detail**: mirrors the web UI's layout (`ADR-0008` calls the P4 web page
  "the brand/UX reference every page mirrors" — reuse that as the native UI's visual
  reference too, not a copy-paste but the same information hierarchy): per-output
  cards (enable, cable, division, nudge, swing, quantized transport button),
  metronome (on/off, vol, voice, accent), Follow Beat readout, tick/phase health as
  a collapsed diagnostics section.
- **Quantized transport buttons**: replicate `transport_led.c`'s three states in the
  UI (dark stopped / blinking armed / solid running) — a `/transport` POST doesn't
  take effect until the next bar, and without the "armed" affordance a tap reads as
  a dead button, the exact UX bug that ticket was written to fix in hardware.
- **OTA**: file picker for a `.bin` (built via `idf.py build`, matching the exact
  target chip — nothing in the protocol prevents uploading the wrong target's binary,
  the app should sanity-check `fw`/target metadata before offering the upload if
  that becomes available, or at minimum warn).

### Tech

SwiftUI, `Network.framework` for discovery, plain `URLSession` for the REST calls.
No C/C++ dependency, no Link SDK, no App Store licensing question. Ships
independently of Phases 2–3.

---

## Phase 2 — Software "KitchenSync Touch"

Turn the phone into an actual Link peer with a touch transport UI and MIDI clock/
transport output — the same role `KitchenSyncTouch/` plays in hardware
(`docs/plans/2026-07-10-kitchensynctouch-sketch-design.md`), but the phone *is* the
device instead of driving one over the network.

### Why this is architecturally simpler than the firmware

The ESP32 firmwares hand-roll a **receive-only, clean-room** Link implementation
(`link_protocol.c`, `link_measurement*.c`) specifically so the closed firmware never
links Ableton's GPLv2 SDK. An iOS app doesn't need that workaround for tempo/
transport — Ableton ships the real SDK with an iOS platform layer and public
examples (`examples/LinkHut`), and it gives session tempo/phase/start-stop and lets
the app **become a full read/write peer** directly (`Link::captureAppSessionState()`),
none of the custom gossip-parse/ping-pong-measurement/GhostXForm machinery this repo
had to build from scratch to stay receive-only. The one thing to verify (see
Licensing below) is whether Ableton requires a commercial license for a
closed-source App Store binary that embeds Link for tempo/transport only — this is
an extremely common case (most DAW-companion iOS apps ship Link support), which
makes it likely low-friction, but "likely" isn't "confirmed."

### MIDI out

The phone has no DIN jack; the realistic path is a **class-compliant USB-MIDI
interface** (or USB-MIDI host device like a Blokas Midihub, mirroring `P4-003`)
connected via a USB-C/Lightning camera adapter. CoreMIDI enumerates it as a normal
`MIDIDestination` — no custom driver work, unlike the ESP32 firmwares' hand-rolled
USB-MIDI host/DIN UART paths.

Timing/jitter: don't drive MIDI send off a `Timer`/`DispatchQueue` (the ESP32
firmware's own bench numbers show a 1 ms writer task alone produces 373 µs stdev,
1.1 ms peak-to-peak jitter — a general-purpose OS timer will be worse). The
established iOS pattern for tight MIDI timing is to schedule sends from an audio
render callback (`AVAudioEngine`/`AudioUnit` I/O tap) driven by the hardware audio
clock, using `MIDISendEventList` with a host-time timestamp rather than "send now."
That's the same principle Follow Beat already leaned on for the P4 (an audio-domain
clock is steadier than a general-purpose task tick) — worth prototyping and
measuring rather than assuming a number.

### Feature parity target (v1)

Mirrors `KitchenSyncTouch` increment 1–2 scope, not the full KitchenSync hub:
- Join a Link session, display tempo/phase/peers.
- MIDI clock (0xF8) + transport (0xFA/0xFC) out to the connected USB-MIDI
  destination, phase-locked to the session (reuse the "arm on this bar" quantized
  launch semantics from `transport_launch.h` / `ktouch_transport.h` — the mailbox
  pattern translates directly to a Swift actor).
- Touch UI: PLAY/STOP with the same dark/blinking-armed/solid-running affordance as
  `transport_led.c`.

Not v1: web config UI (irrelevant, it's a phone app with its own native UI),
multiclock fan-out, swing, metronome speaker output (KitchenSync-hub-tier features;
revisit only if this proves useful as a Touch replacement rather than a fleet
controller).

### Stretch (aligns with existing roadmap, don't build first)

Publishing MIDI clock **in** from the interface into the Link session as a
tempo-setting peer is exactly `P4-011` stage 2 — "not started," "the hard part,"
same tempo-source-arbiter problem noted for the P4. Don't attempt this before the
firmware side has proven the arbiter design; duplicate the same unsolved problem in
two places is a net loss, not two shots at it.

---

## Phase 3 — Audio input → Link Audio

The iOS-side twin of `P4-032`/`LinkAudioPoC`, with the phone's own mic or an
external interface as the source. This is a **proven concept**, not a research
project — `LinkAudioPoC/README.md` documents all three tiers passing on the P4:
SDK runs as a tempo peer, the channel appears in Live, mic audio arrives beat-
stamped. The engineering risk on iOS is smaller in one respect (CoreAudio's
hardware-locked clock is steadier than the P4's WiFi/I2S path that `LinkAudioPoC`
had to fight) and about the same in another (still novel beat-accurate
timestamping — see below).

### Signal path

1. `AVAudioSession` category `.playAndRecord`/`.record`; select the external USB
   audio interface as input route (standard CoreAudio route selection — no custom
   driver, unlike the ESP32 firmwares' planned USB Audio Class **host** driver
   work in `P4-032` Tier 4, which iOS gives for free).
2. `AVAudioEngine` input tap or a lower-latency `AudioUnit` render callback pulls
   PCM buffers.
3. Feed `LinkAudioSink::BufferHandle::write(samples, beatsAtBufferBegin, numFrames,
   channels, sampleRate)` — same public API `LinkAudioPoC/main/main.cpp` already
   calls on the P4.
4. **Reuse `LinkAudioPoC/main/beat_stamper.{h,c}` unchanged.** It's already pure,
   portable C, host-tested (`test/test_beat_stamper.c`), and it owns exactly the
   hard problem here too: keep `beatsAtBufferBegin` continuous (anchor to measured
   session beat, advance by `frames/rate`, resync only past 0.25 beat drift). No
   reason to re-derive this logic for iOS — link the file into the Xcode target the
   same way KitchenSync links shared pure C by relative path (`ADR-0006`/`ADR-0007`
   precedent).

### What's genuinely new work vs. reuse

| Piece | Status |
|---|---|
| `beat_stamper` (the timestamping policy) | **Reuse as-is** — pure C, already proven |
| Link/LinkAudio SDK integration | New iOS platform glue, but Ableton ships official iOS examples — not a from-scratch port like the ESP32 experimental platform layer was |
| Audio capture from an external interface | Standard `AVAudioEngine`/CoreAudio — no custom USB Audio Class driver needed (iOS handles UAC natively), which is strictly easier than what `P4-032` Tier 4 still has to build in firmware |
| Sync accuracy verification | Same gap flagged in `LinkAudioPoC/README.md` — "verified by ear only." Do this properly for the phone build; it's a cheap, high-value measurement (compare a click track sent Link Audio vs. received in Live, ideally with the same kind of scope/analyzer rig this repo already uses for MIDI jitter) |

### Licensing — the actual gate

`LinkAudioPoC/README.md` and the feasibility doc (`docs/plans/2026-07-08-p4-link-audio-feasibility.md`)
are explicit: Link is GPLv2, Link Audio is young/beta, and **shipping a closed
product requires Ableton's commercial license** — "email link-devs@ableton.com
BEFORE productizing," a business gate three P4 tickets are already blocked on. An
App Store binary is exactly the closed-distribution case that license question is
about, and it applies with *more* force here than to Phase 2 (tempo/MIDI-only Link
use is common and low-friction industry-wide; Link **Audio** specifically is new
enough that this repo's own research flagged it as unresolved). Don't build Phase 3
past a personal/dev-only build until that email is answered — same rule already
governing the firmware side, no reason for the app to be exempt.

---

## Cross-cutting

- **Repo/location**: this plan lives in `link-devices` because it directly reuses
  this repo's protocol (`ks_web.cpp`) and pure C (`beat_stamper.c`), and because
  Phases 2–3 are the mobile-side continuation of tickets already tracked here
  (`P4-011`, `P4-032`). Whether the app's *code* lives in this repo or a new one is
  a smaller decision — Xcode projects don't mix well with this repo's
  ESP-IDF/Arduino build tooling and CI matrix, so a separate repo (with this repo
  as a subdirectory/submodule dependency for the shared C files) is the likely
  right call, worth confirming before Phase 1 implementation starts.
- **Reused assets**: `ks_web.cpp` protocol (Phase 1), `transport_launch.h`/
  `ktouch_transport.h` quantized-launch pattern and `transport_led.c` state
  affordance (Phase 2), `beat_stamper.c` (Phase 3). Nothing else in the shared pure
  C layer (`clock_ticker`, `clock_output`, `swing`, the clean-room Link stack) is
  needed by the phone app — it either doesn't apply (no hardware clock output to
  quantize) or is superseded by the official Link SDK doing the same job natively.
- **Sequencing recommendation**: ship Phase 1 first — real value, zero legal gate,
  and it's the one piece every KitchenSync owner wants regardless of what happens
  with Phases 2–3. Send the Ableton licensing email in parallel with Phase 1
  development (it's free and can only block Phases 2–3, not 1); by the time Phase 1
  ships there should be an answer informing whether Phase 2 can proceed on the
  official SDK and whether Phase 3 is viable at all for a distributed app.

## Open questions

- Ableton Link (tempo/transport-only) commercial license terms for a closed iOS
  app — separate question from the Link Audio license already being chased for
  P4-032; confirm both, don't assume tempo-only inherits Link Audio's answer.
- Does the app need to support X32Link units too (fleet includes both), or is scope
  intentionally KitchenSync-only? The `/status` shape differs
  (`web_status_json.c` vs `ks_status.c`) but overlaps enough (`bpm`, `fw`, tick
  health) that supporting both later is cheap if Phase 1's client is written against
  a shared subset — worth deciding before the Swift model is locked in, not after.
- USB-MIDI-out timing target for Phase 2 — no number to design against yet; the
  373 µs firmware figure is a *floor* to beat, not a spec, and should be measured on
  a real phone + interface before committing to an architecture.
