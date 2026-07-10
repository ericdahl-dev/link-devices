# LinkAudioPoC — P4-032: onboard mic → Ableton Link Audio → Live

**Status: PoC PROVEN, 2026-07-09.** All three tiers passed in one session on a
Waveshare ESP32-P4-NANO over WiFi (C6/ESP-Hosted) against Ableton Live 12.4:

| Tier | Exit criterion | Result |
|---|---|---|
| 1 | Official Ableton Link SDK runs as a tempo peer on the P4 | ✓ first build — tempo/beats tracked a live session, no SDK code changes |
| 2 | `LinkAudio` sink's channel visible in Live | ✓ "P4 Mic" appeared; P4 also discovered Live's own five channels |
| 3 | Mic audio arrives in Live, beat-stamped | ✓ audible in Live; commit counter ran at block rate (source subscribed + consuming) |

The feasibility doc (`docs/plans/2026-07-08-p4-link-audio-feasibility.md`)
expected Ethernet would be needed; WiFi via the C6 carried it fine at 16kHz
mono. Ethernet remains the right call for stage-grade jitter, not for the PoC.

## Building

```
# Ableton Link SDK is a gitignored clone -- fetch + patch it once:
git clone --recurse-submodules https://github.com/Ableton/link third_party/link
git -C third_party/link apply ../../patches/ableton-link-esp32-fixes.patch

idf.py set-target esp32p4
# set CONFIG_POC_WIFI_SSID / CONFIG_POC_WIFI_PASSWORD locally (never commit)
idf.py build flash
```

GPLv2 note: the SDK stays out of the committed tree and out of the KitchenSync
production firmware. Shipping a closed product on this requires Ableton's
commercial Link license (link-devs@ableton.com) — email BEFORE productizing.

## What the port actually required (all of it found the hard way)

1. **SDK asio task stack, 8K → 16K** (`patches/ableton-link-esp32-fixes.patch`).
   The SDK's esp32 `Context` spawns its io task — confusingly named "link" —
   with a hardcoded 8192-byte stack sized for base Link. Every LinkAudio
   processor (SinkProcessor/Resizer/PCMEncoder) executes on that task and
   overflows it: hardware stack-protection fault with a constant ~2KB overshoot
   no matter how big the *app's* task stack is. Also renamed it "link_io" so
   panics distinguish it from app tasks.

2. **Keep the P4's 8KB TCM block out of the heap** (`main/main.cpp`,
   `SOC_RESERVE_MEMORY_REGION`). TCM's capability set includes
   `MALLOC_CAP_INTERNAL`, so the best-fit allocator can place a small FreeRTOS
   task stack there (observed: an ESP-Hosted 5120B stack). The first flash op
   then asserts (`esp_task_stack_is_sane_cache_disabled`) — task stacks in TCM
   aren't valid with the cache off. Which build loses this allocation lottery
   is luck; reserving the region removes the dice.

3. **Heap-allocate the `LinkAudio` object** — belt-and-braces after the stack
   hunt; it's only ~1KB but there's no reason for it on a task stack.

4. **`if_nametoindex`/`if_indextoname` stubs** — lwIP lacks them; upstream's
   own example stubs them identically.

## beat_stamper (the one pure module — host-tested)

`main/beat_stamper.{h,c}`, tests in `test/test_beat_stamper.c`. Owns the novel
problem: `beatsAtBufferBegin` must be *continuous* in beat time (Live
reassembles the stream from these stamps; jitter = audible glitch), while the
measurement — session beat at DMA-read return — jitters with scheduling and
the I2S crystal drifts against the Link timeline. Policy: anchor first block
to measured, then advance by exactly `frames/rate × bps`, resync only when
prediction and measurement diverge past 0.25 beat.

## Mic path

Reuses `KitchenSync/main/i2s_audio_bus.c` by relative path — the
P4-020-hardware-validated ES8311 bring-up (24dB mic PGA, analog mic mode,
TX-enabled-as-clock-driver, mono ADC duplicated into both I2S slots). 16kHz
mono, 256-frame (~16ms) blocks, left slot.

## Not yet done (before this graduates past PoC)

- Sync accuracy is verified by ear only — no measured beat-alignment error.
- 16kHz mono PoC fidelity; 48kHz needs a codec reclock + bigger sink buffers.
- Ethernet transport untested (RJ45 present on the board).
- Ableton licensing conversation (ship-gate).
- KitchenSync integration deliberately out of scope (GPLv2 + RAM headroom).
