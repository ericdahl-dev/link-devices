# KitchenSync (P4) — P4-020 Follow Beat: mic-based tempo detection, v1 design

**Date:** 2026-07-09
**Status:** design validated, not yet implemented
**Board:** Waveshare ESP32-P4-NANO (KitchenSync, ESP-IDF target). ES8311 codec, mic-in on I2S DIN=GPIO11 (currently unused — see `metronome_audio.c`).
**Relation to P4-032:** this is the deliberately small first step before the Link Audio PoC
(`docs/plans/2026-07-08-p4-link-audio-feasibility.md`) — it proves mic capture + tempo extraction
work on this hardware before anything depends on the result.

---

## Scope (v1)

**Detect and display only.** Mic → estimated BPM + confidence, shown on the web `/status` page.
Nothing in the clock-output / metronome / Link path reads this value yet — it does not become a
new `beat_source` basis in v1. That's a deliberate follow-on step, not part of this design.

Gated by a new config toggle (`follow_beat_enable`, default off) — mirrors `metronome_enable`.
When off, the feature does not touch I2S/DMA at all (no idle CPU/power cost).

## Architecture

Same pure/impure split as every other engine in this codebase (`beat_source`, `metronome`,
`clock_output`):

```
I2S mic (ES8311, GPIO11)
  -> follow_beat_io.c   (impure: I2S RX task, ring buffer)      -- new, KitchenSync/main/
  -> follow_beat.c/h    (pure: envelope + autocorrelation -> BPM/confidence) -- new, KitchenSync/main/
  -> ks_status.c         (adds follow_bpm / follow_confidence / follow_valid to /status JSON)
  -> ks_web.cpp + ks_config.h/.c  (adds follow_beat_enable toggle, live-safe)
```

### `follow_beat_io.c` (impure)

- New FreeRTOS task, independent of WiFi/Link state (same reasoning as `battery_task`/`bpm_task`:
  hardware readout must not gate on network status).
- Owns I2S RX init on the existing ES8311 codec (mono, 16kHz — reuses `metronome_audio.c`'s
  `SAMPLE_RATE`), pushes fixed-size int16 PCM blocks into a plain ring buffer.
- Task only starts when `cfg->follow_beat_enable` is true; stops/tears down I2S RX when toggled off
  (checked each config-generation change, same pattern as `ks_tick`'s `seen_gen`).

### `follow_beat.c` (pure, host-tested)

Input: raw PCM block (int16 mono). Two stages:

1. **Envelope extraction** — rectify + one-pole low-pass (~10-20Hz cutoff), decimated down to
   ~100Hz. We only need the rhythm envelope, not audio fidelity.
2. **Autocorrelation** — rolling ~4s window of the envelope; search lags corresponding to the
   60-200 BPM range; pick the strongest peak. Confidence = peak-to-mean ratio of the
   autocorrelation function.

Output struct:

```c
typedef struct {
    float bpm;
    float confidence;
    bool  valid;   // true only once confidence clears a threshold
} FollowBeatOut;
```

`valid` gating follows the same "don't report garbage" discipline as
`tempo_source_phase_valid()` in X32Link.

No `KsTickState`/`beat_source` coupling in v1 — this module runs alongside the existing tick
pipeline and only reports outward to status, per the "display only" scope decision above.

## Config + status surface

`ks_config.h` — one new field, wired through the existing three touch points (no new machinery):

```c
int follow_beat_enable;  // 0/1 — mic-based tempo detection (P4-020)
```

- `ks_config_defaults`: default `0` (off).
- `ks_config_set` / `ks_form.c`: goes through the existing single POST-body-grammar intake
  (ARC-006) — no new parsing path.
- `ks_config_live_safe_copy`: live-safe (toggling doesn't need a reboot), same as
  `metronome_enable`.

`ks_status.c` JSON gains `follow_bpm`, `follow_confidence`, `follow_valid`, populated straight from
`FollowBeatOut` — same shape as the existing `bpm`/`peers`/`locked` fields.

Web status page gets one read-only row: `Follow Beat: 128.3 BPM (locked)` when valid, `Follow
Beat: listening...` when not — plus the enable checkbox. No other controls in v1.

## Testing

- `follow_beat.c` is pure (no ESP-IDF dependency) → Unity suite in `test/test_follow_beat.c`,
  same pattern as every other pure module. Feed synthetic envelope streams (generated click train
  at a known BPM + noise) and assert detected BPM/confidence land in range. This is the real
  regression coverage — real-mic behavior can't be asserted in CI.
- `follow_beat_io.c` (I2S glue): untested by unit tests, same as `metronome_audio.c` — hardware-only
  validation.
- Hardware validation: play a metronome/DAW click track at a few known BPMs (90/120/140), confirm
  reported BPM converges within a few seconds and stays stable.

## Open question carried over from the P4-032 feasibility doc

Whether a MEMS mic capsule is actually populated on the P4-NANO board feeding the ES8311's mic-in
path (vs. line-in only) is still unconfirmed — same open question flagged in
`docs/plans/2026-07-08-p4-link-audio-feasibility.md`. Worth a quick continuity/level check on real
hardware before writing `follow_beat_io.c`, since it decides whether v1 needs a physical mic wired
in some other way.

## Explicitly out of scope (v1)

- Driving `beat_source`/clock outputs/metronome from the detected tempo (follow-on work, needs a
  source-select policy + confidence gating + fallback behavior once Link session state and mic
  detection can disagree).
- Any DSP beyond envelope + autocorrelation (no onset-histogram, no ML).
- Stereo / higher sample rates — mono 16kHz throughout, matching the existing codec config.

## Addendum (2026-07-09): shared I2S bus, not mutual exclusion

Consulted the Embedded Firmware Engineer agent on whether `follow_beat_io.c` (RX)
and `metronome_audio.c` (TX) could each independently own an I2S_NUM_0 channel.
Confirmed this is a real hardware conflict, not just an API restriction: both
would be `I2S_ROLE_MASTER` driving the same physical BCLK/WS pins (GPIO12/10) to
the ES8311, which is bus contention — not something the driver can catch, and it
corrupts clocking on both sides.

Fix: a single new module, `i2s_audio_bus.c`, becomes the one owner of
`i2s_new_channel()` (full-duplex, one call, one shared clock generator) and the
ES8311 I2C bring-up. `metronome_audio.c` and `follow_beat_io.c` stop allocating
their own channels/codec handles — they become consumers that call
`i2s_channel_enable()`/`disable()` on the shared TX/RX handles as their own
feature turns on/off. This supersedes the original design's implicit assumption
that the two features would never run concurrently — they now safely can.
