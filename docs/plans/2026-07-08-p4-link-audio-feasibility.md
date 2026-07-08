# KitchenSync (P4) — Ableton Link Audio feasibility + phased plan

**Date:** 2026-07-08
**Status:** planning only — not building yet. Feasibility scoped against the real SDK.
**Board:** Waveshare ESP32-P4-NANO (KitchenSync, ESP-IDF target). Wired Ethernet + PoE, ES8311 codec, USB 2.0 HS OTG Type-A host.
**SDK inspected:** `github.com/Ableton/link` @ HEAD (shallow clone, 2026-07-08).

---

## TL;DR

Product idea: a hardware box that bridges **MIDI ↔ Link** and publishes **audio → Link Audio**
onto the network, so a KitchenSync becomes a wired Link Audio endpoint Ableton Live hears directly.

The scary question was "can Ableton's PC-targeted Link Audio stack run on a microcontroller?"
Reading the SDK, the answer is **much more yes than expected**:

1. **Link Audio reuses the exact same portability seam as base Link.** Everything in
   `include/ableton/link_audio/` is templated on `platform::IoContext` and dependency-injected
   via `util::Injected<IoContext>` — same abstraction base Link already uses. **No raw
   `std::thread` or hardcoded OS dependency in the Link Audio layer.** The port is not a rewrite.
2. **An ESP32 platform layer already exists** (`include/ableton/platforms/esp32/Esp32.hpp`,
   `examples/esp32/`) — though experimental, WiFi-based, Xtensa, ESP-IDF v4.3.1 (2021), base-Link
   tempo only.
3. **Ethernet + PoE on the P4-NANO kills the jitter risk** that would have sunk WiFi PCM streaming.
   Bandwidth was never the issue (~1.5 Mbit/s for stereo 48k int16).
4. **The onboard ES8311 mic collapses the riskiest tier** — proof-of-concept audio input needs no
   USB Audio Class host driver; it reuses the I2S/codec bring-up already done in **P4-006**.

The one non-technical gate: **Link is GPLv2; shipping closed hardware needs Ableton's commercial
Link license, and the Link Audio SDK is still young.** Email Ableton Link licensing before real
investment — it can veto independent of engineering.

---

## What Link Audio is (verified facts)

- Streams **real audio** over the LAN, layered on a normal Link session (tempo/phase/transport
  unchanged). Shipped in Live 12.4 / Push 2.4 / Move 2.0 / Note 2.0.
- Transport is **uncompressed interleaved 16-bit signed PCM** — confirmed `Codec::kPCM_i16` in
  `include/ableton/link_audio/PCMCodec.hpp`, bounded by `AudioBuffer::kMaxAudioBytes`. No codec CPU
  cost: it's memcpy + UDP.
- **Publish/subscribe by channel ID.** Sending side = "sink" (`LinkAudioSink`), receiving side =
  "source" (`LinkAudioSource`). A sink sends **only if at least one source subscribes** — zero idle
  bandwidth. One-to-many distribution.
- **Latency-compensated, sample-accurate**, riding the Link timeline.
- Public API (`include/ableton/LinkAudio.hpp`): `LinkAudio` is a drop-in replacement for `Link`.
  The write path is:
  `LinkAudioSink::BufferHandle::write(samples, beatsAtBufferBegin, numFrames, numChannels {1|2}, sampleRate)`
  on `int16_t` buffers.
- Reference audio example: `extensions/abl_link/examples/link_audio_hut`.

## The key architectural finding

`link_audio/` is fully templated on `IoContext` (see `Controller.hpp`, `SourceProcessor.hpp`,
`SinkProcessor.hpp`, `PeerGateways.hpp`, `Channels.hpp` — all take `util::Injected<IoContext>` and
use `IoContext::Timer`). This is the *same* dependency-injection seam base Link ports through. So
if the ESP32 `IoContext` carries base Link, it is structurally what Link Audio needs too. The port
risk drops from "port a PC audio stack" to "confirm the ESP32 platform layer has headroom for the
extra sockets / timers / DSP processors."

## What already exists in-repo

| Piece | Path | State |
|---|---|---|
| Link Audio core | `include/ableton/link_audio/*` | Public, header-based, IoContext-templated |
| Link Audio public API | `include/ableton/LinkAudio.hpp` / `.ipp` | `LinkAudio` / `LinkAudioSink` / `LinkAudioSource` |
| C API + audio example | `extensions/abl_link/` (`examples/link_audio_hut`) | Reference to mirror |
| ESP32 platform layer | `include/ableton/platforms/esp32/Esp32.hpp` | Experimental |
| ESP32 example | `examples/esp32/` | **base Link only**, WiFi, Xtensa, IDF v4.3.1, tempo sync (`captureAudioSessionState`) — no audio |

---

## Phased plan (four tiers)

### Tier 1 — base Link on the P4 over Ethernet
Un-bit-rot `examples/esp32`: retarget **Xtensa → P4 RISC-V**, bump **ESP-IDF v4.3.1 → v5.x**, switch
**WiFi → Ethernet**. Honor the P4-NANO min-rev flags (`CONFIG_ESP32P4_SELECTS_REV_LESS_V3`,
`CONFIG_ESP32P4_REV_MIN_100` — early silicon rev v1.3). Proves the `IoContext`/`Clock` platform
layer still holds on current toolchain + new silicon.
**Exit:** P4 appears as a tempo peer in a live Link session.

### Tier 2 — Link Audio silent sink, visible in Live
Swap `Link` → `LinkAudio`, create one `LinkAudioSink`, publish silence. Because Link Audio shares
the IoContext seam, this is validating *headroom*, not writing a stack: confirm the ESP32 IoContext
carries the extra `Receivers` sockets, timers, and `MainProcessor`/`SinkProcessor` load without
blowing stack/RAM (P4 + PSRAM should be comfortable).
**Exit:** the channel appears in **Live 12.4** on the LAN.

### Tier 3 (revised) — onboard mic → Link Audio → Live hears it
Feed `LinkAudioSink::BufferHandle::write()` from the **ES8311 mic ADC** (I2S read) with correct
`beatsAtBufferBegin` timestamps. This proves the entire product thesis with **zero USB work**.
Reuses the ES8311/I2S bring-up from **P4-006** (codec I2C addr 0x18, `SCL=GPIO8 SDA=GPIO7`,
NS4150B amp) — P4-006 did the DAC/speaker side; this adds the ADC/record side of the same codec.
The I2S clock is a *known, steady* domain, which makes it the easiest possible source to prove
beat-accurate alignment against (easier than an async USB interface's clock).
**Exit:** recognizable, in-sync mic audio arrives in Live from the P4.
**Caveats:** ES8311 is mono, PoC fidelity; sample rate = whatever the codec is clocked at
(16k/48k), passed as the `sampleRate` arg. None of that blocks the PoC.

### Tier 4 — productization: USB Audio Class interface ingest
Replace the mic with a **UAC1/UAC2 host** path pulling PCM from an external audio interface.
Self-contained driver problem with **no Link-side unknowns**. Reminder (P4-NANO USB host gotcha):
the interface must wire **directly** to the Type-A port — the P4 USB host does not support a hub's
transaction translator. Also the MIDI ↔ Link bridge (the other half of the box) lands around here.

---

## The novel risk that never goes away

The genuinely new engineering is **beat-accurate timestamping** of the audio buffers
(`beatsAtBufferBegin`). Everything else is either reuse (Link's IoContext seam, P4-006 codec) or a
bounded driver problem (UAC host). Tier 3 exists specifically to attack this risk with the simplest
possible input.

## Open questions

- **ES8311 record path:** P4-006 resolved the playback side; confirm the ADC/mic wiring + I2S RX
  config on the NANO schematic (is a MEMS mic populated, or line-in only?).
- **IoContext headroom:** does the existing esp32 `IoContext` need a bigger task/stack or a second
  socket-service task under Link Audio's `Receivers`/`PeerGateways` load?
- **RAM budget:** jitter buffers / `Queue` / `Resizer` sizing vs P4 PSRAM.
- **Licensing:** Ableton commercial Link license terms for closed hardware; Link Audio SDK beta
  stability. **Ask before building.**

## Next actions (when we decide to build)

1. Email Ableton Link licensing (business gate — do first, it's free and can veto).
2. Spin Tiers 1–3 into `P4-0xx` tasks (next free id after P4-031) with `depends_on` chaining.
3. Tier 1 spike: resurrect `examples/esp32` on IDF v5.x for the P4 over Ethernet.

## Sources

- Link Audio FAQ — https://help.ableton.com/hc/en-us/articles/25425913328924-Link-Audio-FAQ
- Ableton/link SDK — https://github.com/Ableton/link
- Live 12.4 Link Audio (CDM) — https://cdm.link/ableton-live-12-4-adds-link-audio-updated-effects-hands-on-details/
- Link Audio in Max/Pd/VCV/TouchDesigner/plugins (CDM) — https://cdm.link/void-link-audio/
- ESP32-P4-NANO (Waveshare) — https://www.waveshare.com/wiki/ESP32-P4-Nano-StartPage
