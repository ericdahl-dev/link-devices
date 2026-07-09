# P4-020 Follow Beat (mic tempo detection, display-only v1) Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Detect BPM from the onboard ES8311 mic and show it on KitchenSync's web `/status` page, with zero effect on the clock/metronome/Link path (v1 is display-only).

**Architecture:** A pure, host-tested `follow_beat.c` (envelope extraction + autocorrelation, 60-200 BPM) mirrors the `beat_source.c`/`metronome.c` pattern. A new shared `i2s_audio_bus.c` becomes the single owner of I2S_NUM_0 + the ES8311 codec (full-duplex, one `i2s_new_channel` call) so the existing TX-only metronome and the new RX-only mic capture can coexist without a hardware clock conflict — confirmed with the Embedded Firmware Engineer consult (2026-07-09): two independent masters can't share BCLK/WS, so `metronome_audio.c` is refactored to consume a shared TX handle instead of owning its own channel.

**Tech Stack:** ESP-IDF (C), Unity host tests (gcc), `i2s_std` driver, ES8311 codec driver.

**Consulted:** Embedded Firmware Engineer agent, 2026-07-09 — confirmed the I2S full-duplex requirement and the shared-bus-module fix (see design doc addendum in Task 1).

---

### Task 1: Design doc addendum — shared I2S bus decision

**Files:**
- Modify: `docs/plans/2026-07-09-p4-020-follow-beat-design.md`

**Step 1: Add the addendum**

Append this section to the end of the design doc (replacing the "Open question carried over..." section's implied mutual-exclusion assumption — the hardware conflict is real, and the fix is a shared bus owner, not mutual exclusion):

```markdown
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
```

**Step 2: Commit**

```bash
cd /Users/edahl/Documents/GitHub/link-devices/.worktrees/p4-020-follow-beat
git add docs/plans/2026-07-09-p4-020-follow-beat-design.md
git commit -m "docs: P4-020 design addendum -- shared I2S bus, not mutual exclusion"
```

---

### Task 2: Pure module `follow_beat` — write the failing tests first

**Files:**
- Create: `test/test_follow_beat.c`
- Create (referenced, doesn't exist yet): `KitchenSync/main/follow_beat.h`, `KitchenSync/main/follow_beat.c`

**Step 1: Write the failing test file**

Create `test/test_follow_beat.c`:

```c
// Host tests for the pure mic tempo detector (P4-020, "Follow Beat" v1:
// detect + display only). follow_beat owns envelope extraction (rectify +
// one-pole lowpass, decimated to a fixed analysis rate) and autocorrelation-
// based BPM estimation over a rolling window. No I2S/ESP-IDF dependency --
// follow_beat_io.c feeds it raw mic samples at the native sample rate.
#include "unity.h"
#include "follow_beat.h"
#include <math.h>
#include <stdlib.h>

static FollowBeat f;
void setUp(void)    { follow_beat_reset(&f); }
void tearDown(void) {}

// Feed n_samples of a synthetic click train: a short decaying impulse every
// `period_samples`, near-silence otherwise (small fixed dither, not zero --
// real mic input is never a flat line, and the envelope's one-pole lowpass
// should reject dither as noise floor either way). Returns the last output.
static FollowBeatOut feed_click_train(int period_samples, int n_samples) {
    FollowBeatOut out = {0};
    for (int i = 0; i < n_samples; i++) {
        int16_t s = (i % period_samples == 0) ? 20000 : ((i % 7 == 0) ? 40 : 0);
        out = follow_beat_push_sample(&f, s);
    }
    return out;
}

// Before the rolling window fills (< FOLLOW_BEAT_ENV_WINDOW_S seconds of
// audio), there's not enough data for a real estimate.
void test_not_valid_before_window_fills(void) {
    // 8000 samples at 16kHz = 0.5s -- well short of the 4s window.
    FollowBeatOut out = feed_click_train(8000, 8000);
    TEST_ASSERT_FALSE(out.valid);
}

// 120 BPM: one beat every 0.5s = 8000 samples at 16kHz. After a few full
// windows (10s of audio), the detector should converge on ~120 BPM with
// enough confidence to report valid.
void test_detects_120_bpm(void) {
    FollowBeatOut out = feed_click_train(8000, 16000 * 10);
    TEST_ASSERT_TRUE(out.valid);
    TEST_ASSERT_TRUE(fabsf(out.bpm - 120.0f) < 3.0f);
}

// 90 BPM: one beat every 0.6667s = 10667 samples at 16kHz.
void test_detects_90_bpm(void) {
    FollowBeatOut out = feed_click_train(10667, 16000 * 10);
    TEST_ASSERT_TRUE(out.valid);
    TEST_ASSERT_TRUE(fabsf(out.bpm - 90.0f) < 3.0f);
}

// 150 BPM: one beat every 0.4s = 6400 samples at 16kHz.
void test_detects_150_bpm(void) {
    FollowBeatOut out = feed_click_train(6400, 16000 * 10);
    TEST_ASSERT_TRUE(out.valid);
    TEST_ASSERT_TRUE(fabsf(out.bpm - 150.0f) < 3.0f);
}

// Pure noise (no periodic structure) must never cross the confidence
// threshold -- "don't report garbage" is the whole point of `valid`.
void test_noise_never_valid(void) {
    FollowBeatOut out = {0};
    unsigned seed = 12345;
    for (int i = 0; i < 16000 * 10; i++) {
        seed = seed * 1103515245u + 12345u;
        int16_t s = (int16_t)((seed >> 16) & 0x3FF) - 512;  // small pseudo-random noise
        out = follow_beat_push_sample(&f, s);
    }
    TEST_ASSERT_FALSE(out.valid);
}

// reset() clears state -- a detector fed a strong 120 BPM signal, then reset,
// must not still report valid on the very next sample.
void test_reset_clears_state(void) {
    feed_click_train(8000, 16000 * 10);
    follow_beat_reset(&f);
    FollowBeatOut out = follow_beat_push_sample(&f, 0);
    TEST_ASSERT_FALSE(out.valid);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_not_valid_before_window_fills);
    RUN_TEST(test_detects_120_bpm);
    RUN_TEST(test_detects_90_bpm);
    RUN_TEST(test_detects_150_bpm);
    RUN_TEST(test_noise_never_valid);
    RUN_TEST(test_reset_clears_state);
    return UNITY_END();
}
```

**Step 2: Wire the test target into `test/Makefile`**

Add near the other `P4*` variables (after `MIDICLOCKIN`):

```make
FOLLOWBEAT = ../KitchenSync/main/follow_beat.c
```

Add `test_follow_beat` to the `all:` target's dependency list and its `./test_follow_beat` run line (put it next to `test_ks_tick`), and add the build rule near the other `test_ks_*` rules:

```make
test_follow_beat: test_follow_beat.c $(FOLLOWBEAT) $(UNITY)
	$(CC) $(CFLAGS) -I../KitchenSync/main $^ -o $@ -lm
```

**Step 3: Run to verify it fails**

```bash
cd /Users/edahl/Documents/GitHub/link-devices/.worktrees/p4-020-follow-beat/test
make test_follow_beat
```

Expected: FAIL — `follow_beat.h: No such file or directory` (neither the header nor `KitchenSync/main/follow_beat.c` exist yet).

**Step 4: Commit the failing test**

```bash
git add test/test_follow_beat.c test/Makefile
git commit -m "test: add failing host tests for follow_beat (P4-020)"
```

---

### Task 3: Pure module `follow_beat` — implement to make the tests pass

**Files:**
- Create: `KitchenSync/main/follow_beat.h`
- Create: `KitchenSync/main/follow_beat.c`

**Step 1: Write `follow_beat.h`**

```c
#pragma once
// Pure mic-based tempo detector (P4-020, "Follow Beat" v1: detect + display
// only). Owns envelope extraction (rectify + one-pole lowpass, decimated to a
// fixed analysis rate) and autocorrelation-based BPM estimation over a rolling
// window. No I2S/ESP-IDF dependency -- follow_beat_io.c feeds it raw mic
// samples at the native sample rate. Host-tested in test/test_follow_beat.c.
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FOLLOW_BEAT_SAMPLE_RATE    16000  // native input rate (matches metronome_audio.c)
#define FOLLOW_BEAT_ENV_RATE       100    // decimated envelope analysis rate (Hz)
#define FOLLOW_BEAT_ENV_WINDOW_S   4      // rolling autocorrelation window, seconds
#define FOLLOW_BEAT_ENV_LEN        (FOLLOW_BEAT_ENV_RATE * FOLLOW_BEAT_ENV_WINDOW_S)  // 400
#define FOLLOW_BEAT_MIN_BPM        60
#define FOLLOW_BEAT_MAX_BPM        200
#define FOLLOW_BEAT_CONFIDENCE_THRESHOLD 2.5f  // peak/mean ratio needed to report valid

typedef struct {
    float bpm;
    float confidence;   // peak/mean ratio of the autocorrelation search range
    bool  valid;         // true once confidence clears FOLLOW_BEAT_CONFIDENCE_THRESHOLD
} FollowBeatOut;

typedef struct {
    float         env_ring[FOLLOW_BEAT_ENV_LEN];
    int           ring_pos;     // next write index (wraps)
    bool          ring_full;    // has the ring wrapped at least once?
    float         lp_state;     // one-pole lowpass state, rectified-input domain
    int           decim_count;  // samples accumulated toward the next envelope tick
    float         decim_peak;   // peak filtered value seen in the current decim block
    FollowBeatOut last_out;     // published between envelope ticks
} FollowBeat;

void follow_beat_reset(FollowBeat* f);

// Feed one raw mono sample at FOLLOW_BEAT_SAMPLE_RATE. Internally decimates to
// the envelope rate and recomputes the BPM estimate once per envelope tick,
// once the rolling window is full. Returns the latest estimate (unchanged
// between envelope ticks).
FollowBeatOut follow_beat_push_sample(FollowBeat* f, int16_t sample);

#ifdef __cplusplus
}
#endif
```

**Step 2: Write `follow_beat.c`**

```c
#include "follow_beat.h"

#define FOLLOW_BEAT_DECIM (FOLLOW_BEAT_SAMPLE_RATE / FOLLOW_BEAT_ENV_RATE)  // 160

// One-pole lowpass alpha for a ~15Hz cutoff at 16kHz: alpha = dt/(RC+dt),
// RC = 1/(2*pi*fc). We only need the rhythm envelope, not audio fidelity.
#define ENV_LPF_ALPHA 0.006f

void follow_beat_reset(FollowBeat* f) {
    for (int i = 0; i < FOLLOW_BEAT_ENV_LEN; i++) f->env_ring[i] = 0.0f;
    f->ring_pos    = 0;
    f->ring_full   = false;
    f->lp_state    = 0.0f;
    f->decim_count = 0;
    f->decim_peak  = 0.0f;
    f->last_out    = (FollowBeatOut){ .bpm = 0.0f, .confidence = 0.0f, .valid = false };
}

static float rectify(int16_t s) {
    float v = (float)s / 32768.0f;
    return v < 0.0f ? -v : v;
}

// Autocorrelation-based BPM search over the current ring contents. Only
// called once the ring is full (a complete FOLLOW_BEAT_ENV_WINDOW_S window).
// Lags are in envelope samples; bpm = 60 * FOLLOW_BEAT_ENV_RATE / lag.
static FollowBeatOut analyze(const FollowBeat* f) {
    // Unwrap the ring into a linear window -- it's small (400 floats), so a
    // copy is cheap next to the O(lags * window) correlation sum below, and
    // it keeps that loop free of modular indexing.
    float win[FOLLOW_BEAT_ENV_LEN];
    for (int i = 0; i < FOLLOW_BEAT_ENV_LEN; i++)
        win[i] = f->env_ring[(f->ring_pos + i) % FOLLOW_BEAT_ENV_LEN];

    int min_lag = (60 * FOLLOW_BEAT_ENV_RATE) / FOLLOW_BEAT_MAX_BPM;  // 200 BPM -> 30
    int max_lag = (60 * FOLLOW_BEAT_ENV_RATE) / FOLLOW_BEAT_MIN_BPM;  // 60 BPM -> 100

    float corr[FOLLOW_BEAT_ENV_LEN];  // indexed directly by lag (sparse use)
    float best_corr = -1.0f;
    float sum_corr  = 0.0f;
    int   n_lags    = 0;

    for (int lag = min_lag; lag <= max_lag; lag++) {
        float acc = 0.0f;
        int   n   = FOLLOW_BEAT_ENV_LEN - lag;
        for (int i = 0; i < n; i++) acc += win[i] * win[i + lag];
        acc /= (float)n;
        corr[lag] = acc;
        sum_corr += acc;
        n_lags++;
        if (acc > best_corr) best_corr = acc;
    }
    float mean_corr = n_lags > 0 ? sum_corr / (float)n_lags : 0.0f;

    // Octave-error guard: a periodic signal's autocorrelation is also strong
    // at integer multiples of the true period (half-tempo aliasing), so pick
    // the SHORTEST lag that's still nearly as strong as the global peak,
    // rather than the peak itself -- the true beat is the fastest strong one.
    int best_lag = min_lag;
    for (int lag = min_lag; lag <= max_lag; lag++) {
        if (corr[lag] >= 0.9f * best_corr) { best_lag = lag; break; }
    }

    float confidence = (mean_corr > 1e-9f) ? (best_corr / mean_corr) : 0.0f;

    FollowBeatOut o;
    o.bpm        = (60.0f * FOLLOW_BEAT_ENV_RATE) / (float)best_lag;
    o.confidence = confidence;
    o.valid      = confidence >= FOLLOW_BEAT_CONFIDENCE_THRESHOLD;
    return o;
}

FollowBeatOut follow_beat_push_sample(FollowBeat* f, int16_t sample) {
    float rect = rectify(sample);
    f->lp_state += ENV_LPF_ALPHA * (rect - f->lp_state);
    if (f->lp_state > f->decim_peak) f->decim_peak = f->lp_state;

    f->decim_count++;
    if (f->decim_count < FOLLOW_BEAT_DECIM) return f->last_out;
    f->decim_count = 0;

    f->env_ring[f->ring_pos] = f->decim_peak;
    f->decim_peak = 0.0f;
    f->ring_pos = (f->ring_pos + 1) % FOLLOW_BEAT_ENV_LEN;
    if (f->ring_pos == 0) f->ring_full = true;

    f->last_out = f->ring_full
        ? analyze(f)
        : (FollowBeatOut){ .bpm = 0.0f, .confidence = 0.0f, .valid = false };
    return f->last_out;
}
```

**Step 3: Run tests to verify they pass**

```bash
cd /Users/edahl/Documents/GitHub/link-devices/.worktrees/p4-020-follow-beat/test
make test_follow_beat && ./test_follow_beat
```

Expected: `6 Tests 0 Failures 0 Ignored` / `OK`.

If `test_detects_*_bpm` fails on the octave (reports exactly half or double the
expected BPM), that's the guard in `analyze()` misfiring — check the `0.9f`
threshold against the actual `corr[]` values for that period before changing
the confidence math. If `test_noise_never_valid` fails (reports valid on pure
noise), raise `FOLLOW_BEAT_CONFIDENCE_THRESHOLD`.

**Step 4: Run the full host suite to confirm no regressions**

```bash
make all
```

Expected: every suite still `OK`, plus the new `test_follow_beat`.

**Step 5: Commit**

```bash
git add KitchenSync/main/follow_beat.h KitchenSync/main/follow_beat.c
git commit -m "feat: pure follow_beat tempo detector (P4-020)"
```

---

### Task 4: `ks_config` — add `follow_beat_enable` field (TDD)

**Files:**
- Modify: `test/test_ks_config.c`
- Modify: `KitchenSync/main/ks_config.h`
- Modify: `KitchenSync/main/ks_config.c`

**Step 1: Write the failing tests**

Add to `test/test_ks_config.c` (near `test_defaults`):

```c
void test_follow_beat_default_off(void) {
    TEST_ASSERT_EQUAL_INT(0, c.follow_beat_enable);
}

void test_follow_beat_set(void) {
    TEST_ASSERT_TRUE(ks_config_set(&c, "follow_beat", "1"));
    TEST_ASSERT_EQUAL_INT(1, c.follow_beat_enable);
    TEST_ASSERT_TRUE(ks_config_set(&c, "follow_beat", "0"));
    TEST_ASSERT_EQUAL_INT(0, c.follow_beat_enable);
    TEST_ASSERT_FALSE(ks_config_set(&c, "follow_beat", "2"));  // 0/1 only
}
```

Add both `RUN_TEST(...)` lines to `main()`.

**Step 2: Run to verify it fails**

```bash
cd /Users/edahl/Documents/GitHub/link-devices/.worktrees/p4-020-follow-beat/test
make test_ks_config && ./test_ks_config
```

Expected: compile error — `follow_beat_enable` is not a member of `KsConfig`.

**Step 3: Implement**

In `KitchenSync/main/ks_config.h`, add to the `KsConfig` struct (after `led_accent_color`):

```c
    int  follow_beat_enable; // 0/1 -- mic-based tempo detection (P4-020, display only)
```

In `KitchenSync/main/ks_config.c`:

- `ks_config_defaults`: no line needed — `memset(c, 0, sizeof(*c))` already defaults it to `0` (off), matching `metronome_enable`'s "audible/active feature defaults off" convention. Add a one-line comment for clarity, next to the `led_enable` default:

```c
    c->follow_beat_enable = 0;  // mic capture: default off (P4-020)
```

- `ks_config_valid`: add a range check next to `led_enable`'s:

```c
    if (c->follow_beat_enable != 0 && c->follow_beat_enable != 1) return false;
```

- `ks_config_set`: add a branch next to `"led"`:

```c
    if (strcmp(key, "follow_beat") == 0) {
        int v = atoi(value);
        if (v != 0 && v != 1) return false;
        c->follow_beat_enable = v;
        return true;
    }
```

- `ks_config_live_safe_copy`: **do not** add `follow_beat_enable` here. Starting/stopping mic capture involves I2S/codec bring-up (like the metronome), so — matching `metronome_enable`'s precedent of needing a reboot to actually start the codec — this field is Save-and-reboot only in v1, not live-toggle. (This mirrors `wifi_ssid`/`wifi_pass` being deliberately excluded too.)

**Step 4: Run tests to verify they pass**

```bash
make test_ks_config && ./test_ks_config
```

Expected: all tests `OK`, including the two new ones.

**Step 5: Run the full suite + commit**

```bash
make all
git add test/test_ks_config.c KitchenSync/main/ks_config.h KitchenSync/main/ks_config.c
git commit -m "feat: add follow_beat_enable config field (P4-020)"
```

---

### Task 5: `ks_form` — wire the checkbox into the POST-body grammar (TDD)

**Files:**
- Modify: `test/test_ks_form.c`
- Modify: `KitchenSync/main/ks_form.c`

**Step 1: Write the failing test**

Add to `test/test_ks_form.c` (near `test_absent_checkbox_reads_off`):

```c
// follow_beat_enable is a checkbox too: absent from the body -> off.
void test_follow_beat_absent_reads_off(void) {
    base.follow_beat_enable = 1;
    resolve("wifi_ssid=X");
    TEST_ASSERT_EQUAL_INT(0, out.follow_beat_enable);
}

void test_follow_beat_present_reads_on(void) {
    base.follow_beat_enable = 0;
    resolve("follow_beat=1");
    TEST_ASSERT_EQUAL_INT(1, out.follow_beat_enable);
}
```

Add both `RUN_TEST(...)` lines to `main()`.

**Step 2: Run to verify it fails**

```bash
cd /Users/edahl/Documents/GitHub/link-devices/.worktrees/p4-020-follow-beat/test
make test_ks_form && ./test_ks_form
```

Expected: FAIL — `test_follow_beat_absent_reads_off` fails because `ks_form_resolve` doesn't pre-clear `follow_beat_enable` yet (it stays `1` from `base`).

**Step 3: Implement**

In `KitchenSync/main/ks_form.c`'s `ks_form_resolve`, add to the checkbox pre-clear block (next to `out->led_enable = 0;`):

```c
    out->follow_beat_enable = 0;
```

**Step 4: Run tests to verify they pass**

```bash
make test_ks_form && ./test_ks_form
```

Expected: all tests `OK`.

**Step 5: Run the full suite + commit**

```bash
make all
git add test/test_ks_form.c KitchenSync/main/ks_form.c
git commit -m "feat: wire follow_beat_enable through the config form grammar (P4-020)"
```

---

### Task 6: `ks_status` — add `follow_bpm`/`follow_confidence`/`follow_valid` to `/status` (TDD)

**Files:**
- Modify: `test/test_ks_status.c`
- Modify: `KitchenSync/main/ks_status.h`
- Modify: `KitchenSync/main/ks_status.c`
- Modify: `KitchenSync/main/ks_web.cpp:353-356` (call site)

**Step 1: Write the failing tests**

Update every existing call to `ks_status_json` in `test/test_ks_status.c` to pass three new trailing args, and add new assertions. Replace the whole file's body with:

```c
// Host tests for the pure KitchenSync /status JSON builder (P4-007, extended
// P4-020 with the mic tempo-follow fields).
#include "unity.h"
#include "ks_status.h"
#include <string.h>

void setUp(void)    {}
void tearDown(void) {}

// All fields present; usb and follow_valid serialize as real JSON bools.
void test_all_fields_present(void) {
    char b[220];
    ks_status_json(b, sizeof(b), 132.0f, 120.5f, 1, true, 583, "2.1.0", 128.3f, 3.1f, true);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"bpm\":132.0"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"min\":120.5"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"peers\":1"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"usb\":true"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"tx\":583"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"fw\":\"2.1.0\""));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow_bpm\":128.3"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow_confidence\":3.1"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow_valid\":true"));
}

void test_usb_false_is_json_bool(void) {
    char b[220];
    ks_status_json(b, sizeof(b), 0.0f, 0.0f, 0, false, 0, "2.1.0", 0.0f, 0.0f, false);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"usb\":false"));
}

// follow_valid false serializes as the bool false, not 0/"false".
void test_follow_valid_false_is_json_bool(void) {
    char b[220];
    ks_status_json(b, sizeof(b), 0.0f, 0.0f, 0, false, 0, "2.1.0", 0.0f, 0.0f, false);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow_valid\":false"));
}

void test_return_value_is_snprintf_style_length(void) {
    char b[220];
    int n = ks_status_json(b, sizeof(b), 132.0f, 120.5f, 1, true, 583, "2.1.0", 128.3f, 3.1f, true);
    TEST_ASSERT_EQUAL_INT((int)strlen(b), n);

    char tiny[4];
    int n2 = ks_status_json(tiny, sizeof(tiny), 132.0f, 120.5f, 1, true, 583, "2.1.0", 128.3f, 3.1f, true);
    TEST_ASSERT_TRUE(n2 > (int)sizeof(tiny) - 1);  // truncated but length still reported
}

void test_fw_string_passes_through(void) {
    char b[220];
    ks_status_json(b, sizeof(b), 120.0f, 0.0f, 0, false, 0, "9.9.9-rc1", 0.0f, 0.0f, false);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"fw\":\"9.9.9-rc1\""));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_all_fields_present);
    RUN_TEST(test_usb_false_is_json_bool);
    RUN_TEST(test_follow_valid_false_is_json_bool);
    RUN_TEST(test_return_value_is_snprintf_style_length);
    RUN_TEST(test_fw_string_passes_through);
    return UNITY_END();
}
```

**Step 2: Run to verify it fails**

```bash
cd /Users/edahl/Documents/GitHub/link-devices/.worktrees/p4-020-follow-beat/test
make test_ks_status && ./test_ks_status
```

Expected: compile error — too many arguments to `ks_status_json`.

**Step 3: Implement**

`KitchenSync/main/ks_status.h` — replace the function signature and its doc comment:

```c
// Formats {"bpm":F,"min":F,"peers":N,"usb":bool,"tx":N,"fw":"S","follow_bpm":F,
// "follow_confidence":F,"follow_valid":bool} into buf. `bpm` is the live Link
// session tempo, `min` the detected MIDI-clock-IN tempo (0 when no clock in,
// P4-011), `peers` the Link peer count, `usb` whether a USB-MIDI device is
// ready, `tx` the running clock-pulse count, `fw` the firmware version string
// (LNK-038), `follow_bpm`/`follow_confidence`/`follow_valid` the mic tempo-
// follow estimate (P4-020, 0/0/false when the feature is off or not yet
// confident). Returns snprintf()'s return value so the caller can detect
// truncation.
int ks_status_json(char* buf, size_t len, float bpm, float midi_bpm, int peers, bool usb, uint32_t tx,
                   const char* fw, float follow_bpm, float follow_confidence, bool follow_valid);
```

`KitchenSync/main/ks_status.c`:

```c
#include "ks_status.h"
#include <stdio.h>

int ks_status_json(char* buf, size_t len, float bpm, float midi_bpm, int peers, bool usb, uint32_t tx,
                   const char* fw, float follow_bpm, float follow_confidence, bool follow_valid) {
    return snprintf(buf, len,
                    "{\"bpm\":%.1f,\"min\":%.1f,\"peers\":%d,\"usb\":%s,\"tx\":%lu,\"fw\":\"%s\","
                    "\"follow_bpm\":%.1f,\"follow_confidence\":%.1f,\"follow_valid\":%s}",
                    bpm, midi_bpm, peers, usb ? "true" : "false", (unsigned long)tx, fw,
                    follow_bpm, follow_confidence, follow_valid ? "true" : "false");
}
```

**Step 4: Run tests to verify they pass**

```bash
make test_ks_status && ./test_ks_status
```

Expected: all tests `OK`.

**Step 5: Fix the real call site** (this doesn't have a host test — `ks_web.cpp` is ESP-IDF/httpd glue — but it must compile, so update it now):

In `KitchenSync/main/ks_web.cpp`, `status_handler` (around line 350-359), change:

```c
static esp_err_t status_handler(httpd_req_t *req)
{
    char buf[128];
    ks_status_json(buf, sizeof(buf),
                      (float)link_proto_bpm(), midi_clock_in_bpm(esp_timer_get_time()),
                      wifi_link_peers(), usb_midi_host_ready(), usb_midi_host_tx(),
                      FW_VERSION);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}
```

to:

```c
static esp_err_t status_handler(httpd_req_t *req)
{
    char buf[192];   // grew from 128 -- three more fields (P4-020)
    FollowBeatOut fb = s_cfg && s_cfg->follow_beat_enable ? follow_beat_io_status() : (FollowBeatOut){0};
    ks_status_json(buf, sizeof(buf),
                      (float)link_proto_bpm(), midi_clock_in_bpm(esp_timer_get_time()),
                      wifi_link_peers(), usb_midi_host_ready(), usb_midi_host_tx(),
                      FW_VERSION, fb.bpm, fb.confidence, fb.valid);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}
```

Add `#include "follow_beat_io.h"` near the top of `ks_web.cpp`'s includes (this compiles once Task 8 creates that header — if doing tasks strictly in order, come back to this `#include` after Task 8; note it here now so it isn't missed).

**Step 6: Run the full host suite + commit**

```bash
cd /Users/edahl/Documents/GitHub/link-devices/.worktrees/p4-020-follow-beat/test
make all
git add test/test_ks_status.c KitchenSync/main/ks_status.h KitchenSync/main/ks_status.c KitchenSync/main/ks_web.cpp
git commit -m "feat: add follow-beat fields to /status JSON (P4-020)"
```

(`ks_web.cpp` doesn't compile stand-alone yet — it references `follow_beat_io_status`/`FollowBeatOut` from a header that doesn't exist until Task 8. That's fine for a host-test-only commit; the firmware won't compile again until Task 8 lands. If you'd rather keep every commit firmware-buildable, hold this file's edit until Task 8 and note it there instead — either ordering is fine, just don't skip it.)

---

### Task 7: `i2s_audio_bus` — shared full-duplex I2S + codec owner

**Files:**
- Create: `KitchenSync/main/i2s_audio_bus.h`
- Create: `KitchenSync/main/i2s_audio_bus.c`

No host test — this is ESP-IDF hardware glue (I2S driver, I2C, ES8311), same category as `metronome_audio.c` (untested by unit tests, hardware-only validation). Verified in Task 9's hardware step.

**Step 1: Write `i2s_audio_bus.h`**

```c
#pragma once
// KitchenSync shared I2S bus + ES8311 codec owner (P4-020). The ES8311 is one
// physical codec on one I2S bus (MCLK=GPIO13 BCLK=GPIO12 WS=GPIO10, DOUT=GPIO9
// speaker out, DIN=GPIO11 mic in) -- metronome_audio.c (TX) and follow_beat_io.c
// (RX) can't each independently call i2s_new_channel() as I2S_ROLE_MASTER: two
// masters can't share the same physical BCLK/WS pins (confirmed via embedded-
// firmware consult, see docs/plans/2026-07-09-p4-020-follow-beat-design.md
// addendum). This module does ONE i2s_new_channel() call for both directions
// (true full duplex, one shared clock generator) plus the one ES8311 I2C
// bring-up; metronome_audio.c and follow_beat_io.c become consumers that only
// call i2s_channel_enable()/disable() on the handles this module owns.
#include <stdbool.h>
#include "driver/i2s_std.h"
#include "es8311.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_BUS_SAMPLE_RATE   16000
#define AUDIO_BUS_MCLK_MULTIPLE 384

// Bring up I2S_NUM_0 (full duplex) + the ES8311 codec (I2C addr 0x18). Call
// once at boot, before either metronome_audio_start() or follow_beat_io_start()
// -- both now require this to have already run. Idempotent: a second call is a
// no-op (logs and returns) if already ready.
void audio_bus_init(void);

bool audio_bus_ready(void);

// TX (speaker out) / RX (mic in) channel handles. NULL if audio_bus_init()
// hasn't run or failed -- callers must check audio_bus_ready() first.
i2s_chan_handle_t audio_bus_tx(void);
i2s_chan_handle_t audio_bus_rx(void);

// The one ES8311 handle, for volume/mic-path config that must go through the
// codec directly (metronome volume, e.g.).
es8311_handle_t audio_bus_codec(void);

#ifdef __cplusplus
}
#endif
```

**Step 2: Write `i2s_audio_bus.c`**

```c
/*
 * KitchenSync shared I2S bus + ES8311 codec owner (P4-020). See
 * i2s_audio_bus.h for why this exists: metronome_audio.c (TX) and
 * follow_beat_io.c (RX) can't each independently master I2S_NUM_0.
 *
 * Pin map (Waveshare ESP32-P4-NANO, same as metronome_audio.c's original
 * table -- see that file's P4-006 header comment for the full derivation):
 *   I2C:  SCL=GPIO8   SDA=GPIO7   (I2C0, ES8311 @ 0x18)
 *   I2S:  MCLK=GPIO13 BCLK=GPIO12 WS=GPIO10 DOUT=GPIO9 DIN=GPIO11
 */
#include "i2s_audio_bus.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "audio_bus";

#define I2C_PORT      I2C_NUM_0
#define PIN_I2C_SCL   GPIO_NUM_8
#define PIN_I2C_SDA   GPIO_NUM_7
#define PIN_I2S_MCLK  GPIO_NUM_13
#define PIN_I2S_BCLK  GPIO_NUM_12
#define PIN_I2S_WS    GPIO_NUM_10
#define PIN_I2S_DOUT  GPIO_NUM_9
#define PIN_I2S_DIN   GPIO_NUM_11

static i2s_chan_handle_t s_tx = NULL;
static i2s_chan_handle_t s_rx = NULL;
static es8311_handle_t   s_codec = NULL;
static volatile bool     s_ready = false;

static esp_err_t i2s_init(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;  // TX underruns emit silence, not the last sample
    esp_err_t e = i2s_new_channel(&chan_cfg, &s_tx, &s_rx);  // full duplex: one clock domain
    if (e != ESP_OK) return e;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_BUS_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT,
            .din  = PIN_I2S_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = AUDIO_BUS_MCLK_MULTIPLE;

    e = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (e != ESP_OK) return e;
    e = i2s_channel_init_std_mode(s_rx, &std_cfg);
    if (e != ESP_OK) return e;

    // Both channels start disabled -- metronome_audio_start()/follow_beat_io_start()
    // enable their own direction when their feature is actually turned on.
    return ESP_OK;
}

static esp_err_t codec_init(void) {
    const i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    esp_err_t e = i2c_param_config(I2C_PORT, &i2c_cfg);
    if (e != ESP_OK) return e;
    e = i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (e != ESP_OK) return e;

    s_codec = es8311_create(I2C_PORT, ES8311_ADDRRES_0);
    if (!s_codec) return ESP_FAIL;

    const es8311_clock_config_t clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = AUDIO_BUS_SAMPLE_RATE * AUDIO_BUS_MCLK_MULTIPLE,
        .sample_frequency = AUDIO_BUS_SAMPLE_RATE,
    };
    e = es8311_init(s_codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    if (e != ESP_OK) return e;
    return es8311_sample_frequency_config(s_codec, AUDIO_BUS_SAMPLE_RATE * AUDIO_BUS_MCLK_MULTIPLE,
                                           AUDIO_BUS_SAMPLE_RATE);
    // NOTE: es8311_microphone_config()'s bool param meaning (differential vs.
    // single-ended input select -- NOT a mic on/off gate; metronome_audio.c's
    // existing codec_init() already calls it with `false` even though that
    // path is TX-only) needs confirming against the vendored es8311.h before
    // Task 9's hardware bring-up. Call it here once confirmed, matching
    // whatever the board's mic wiring actually needs.
}

void audio_bus_init(void) {
    if (s_ready) { ESP_LOGW(TAG, "already initialized"); return; }

    ESP_LOGI(TAG, "audio bus: ES8311 codec + I2S full duplex on I2S_NUM_0");
    ESP_LOGI(TAG, "  I2C SCL=%d SDA=%d (ES8311 @ 0x%02x)", PIN_I2C_SCL, PIN_I2C_SDA, ES8311_ADDRRES_0);
    ESP_LOGI(TAG, "  I2S MCLK=%d BCLK=%d WS=%d DOUT=%d DIN=%d  %d Hz",
             PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_DIN, AUDIO_BUS_SAMPLE_RATE);

    esp_err_t e = i2s_init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "I2S init failed: %s -- audio bus unavailable", esp_err_to_name(e)); return; }
    e = codec_init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "ES8311 init failed: %s -- audio bus unavailable", esp_err_to_name(e)); return; }

    s_ready = true;
    ESP_LOGI(TAG, "audio bus ready");
}

bool audio_bus_ready(void) { return s_ready; }
i2s_chan_handle_t audio_bus_tx(void) { return s_tx; }
i2s_chan_handle_t audio_bus_rx(void) { return s_rx; }
es8311_handle_t   audio_bus_codec(void) { return s_codec; }
```

**Step 3: Commit**

```bash
cd /Users/edahl/Documents/GitHub/link-devices/.worktrees/p4-020-follow-beat
git add KitchenSync/main/i2s_audio_bus.h KitchenSync/main/i2s_audio_bus.c
git commit -m "feat: shared full-duplex I2S bus + ES8311 owner (P4-020)"
```

---

### Task 8: Refactor `metronome_audio.c` to consume the shared bus

**Files:**
- Modify: `KitchenSync/main/metronome_audio.c`

**Step 1: Remove `metronome_audio.c`'s own channel/codec ownership**

Delete the `i2s_init()` and `codec_init()` static functions and their pin
`#define`s (`I2C_PORT`, `PIN_I2C_SCL`, `PIN_I2C_SDA`, `PIN_I2S_MCLK`,
`PIN_I2S_BCLK`, `PIN_I2S_WS`, `PIN_I2S_DOUT`, `PIN_I2S_DIN`) — those now live
in `i2s_audio_bus.c`. Keep `PIN_PA_ENABLE` (the NS4150B amp GPIO is
metronome-specific, not shared).

Add `#include "i2s_audio_bus.h"`.

Change `metronome_audio_start()`'s body: instead of calling the deleted
`i2s_init()`/`codec_init()`, it now requires the bus already exists and just
enables its TX channel:

```c
void metronome_audio_start(int volume, int voice)
{
    if (!audio_bus_ready()) {
        ESP_LOGE(TAG, "audio bus not ready -- metronome muted (call audio_bus_init() first)");
        return;
    }
    s_volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);

    float click_hz, accent_hz;
    int   click_ms, accent_ms;
    metronome_voice_params(voice, &click_hz, &click_ms, &accent_hz, &accent_ms);
    s_click_frames  = SAMPLE_RATE * click_ms  / 1000;
    s_accent_frames = SAMPLE_RATE * accent_ms / 1000;
    if (s_click_frames  > MAX_FRAMES) s_click_frames  = MAX_FRAMES;
    if (s_accent_frames > MAX_FRAMES) s_accent_frames = MAX_FRAMES;

    ESP_LOGI(TAG, "metronome audio: ES8311 codec + I2S out the onboard speaker  vol=%d voice=%d", s_volume, voice);

    render_burst(s_click,  s_click_frames,  click_hz,  CLICK_AMP);
    render_burst(s_accent, s_accent_frames, accent_hz, ACCENT_AMP);

    /* NS4150B power amp enable (active-high). */
    gpio_config_t pa = {
        .pin_bit_mask = 1ULL << PIN_PA_ENABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pa);
    gpio_set_level(PIN_PA_ENABLE, 1);

    s_tx = audio_bus_tx();
    es8311_voice_volume_set(audio_bus_codec(), s_volume, NULL);
    esp_err_t e = i2s_channel_enable(s_tx);
    if (e != ESP_OK) { ESP_LOGE(TAG, "TX channel enable failed: %s -- metronome muted", esp_err_to_name(e)); return; }

    s_queue = xQueueCreate(4, sizeof(bool));
    if (!s_queue) { ESP_LOGE(TAG, "queue alloc failed -- metronome muted"); return; }
    if (xTaskCreate(player_task, "metro_click", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "player task create failed -- metronome muted");
        return;
    }
    s_ready = true;
    ESP_LOGI(TAG, "metronome audio ready");
}
```

Note `s_codec` (the module-local static) is no longer set here — `metronome_audio_set()` currently calls `es8311_voice_volume_set(s_codec, ...)`; change that one call site to `es8311_voice_volume_set(audio_bus_codec(), ...)` and delete the now-unused `static es8311_handle_t s_codec = NULL;` line.

**Step 2: Verify it still compiles cleanly on its own logic**

There's no host test for this file (hardware glue). Full verification is
Task 9's `idf.py build` + Task 10's on-device check — don't skip those.

**Step 3: Commit**

```bash
git add KitchenSync/main/metronome_audio.c
git commit -m "refactor: metronome_audio consumes the shared I2S bus (P4-020)"
```

---

### Task 9: `follow_beat_io` — mic capture task

**Files:**
- Create: `KitchenSync/main/follow_beat_io.h`
- Create: `KitchenSync/main/follow_beat_io.c`

**Step 1: Write `follow_beat_io.h`**

```c
#pragma once
// KitchenSync mic capture glue (P4-020) -- consumes the shared I2S RX channel
// from i2s_audio_bus.c (ES8311 mic-in, DIN=GPIO11) and feeds the pure
// follow_beat.c tempo detector. Thin per ADR-0003: this file only gets samples
// off the wire; the BPM decision lives in follow_beat.c.
#include <stdbool.h>
#include "follow_beat.h"

#ifdef __cplusplus
extern "C" {
#endif

// Spawn the capture task, which reads from the shared bus's RX channel and
// feeds follow_beat_push_sample(), republishing the latest FollowBeatOut. Call
// once at boot (only when follow_beat_enable is set), after audio_bus_init().
// No-op (logs + returns) if the bus isn't ready.
void follow_beat_io_start(void);

bool follow_beat_io_ready(void);

// Latest published estimate. Single-writer (capture task) / single-reader
// (status poll) via a short critical section -- FollowBeatOut is a small POD
// struct, so this is cheap and avoids a torn read.
FollowBeatOut follow_beat_io_status(void);

#ifdef __cplusplus
}
#endif
```

**Step 2: Write `follow_beat_io.c`**

```c
/*
 * KitchenSync mic capture glue (P4-020) -- reads the shared I2S RX channel
 * (i2s_audio_bus.c, ES8311 mic-in DIN=GPIO11) and feeds the pure follow_beat.c
 * tempo detector. See i2s_audio_bus.h for why this doesn't own its own I2S
 * channel/codec handle.
 */
#include "follow_beat_io.h"
#include "i2s_audio_bus.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "follow_beat_io";

#define READ_BLOCK_FRAMES 256  // ~16ms per block at 16kHz mono

static TaskHandle_t  s_task = NULL;
static volatile bool s_ready = false;
static FollowBeat     s_detector;
static FollowBeatOut  s_latest;
static portMUX_TYPE   s_lock = portMUX_INITIALIZER_UNLOCKED;

static void capture_task(void *arg) {
    (void)arg;
    i2s_chan_handle_t rx = audio_bus_rx();
    // AUDIO_BUS is configured I2S_SLOT_MODE_STEREO (metronome's TX needs
    // stereo out); read stereo frames and use the left channel only -- the
    // mic is wired to one physical channel regardless of slot count.
    int16_t block[READ_BLOCK_FRAMES * 2];
    size_t  bytes_read = 0;
    for (;;) {
        esp_err_t e = i2s_channel_read(rx, block, sizeof(block), &bytes_read, portMAX_DELAY);
        if (e != ESP_OK) { ESP_LOGW(TAG, "i2s read failed: %s", esp_err_to_name(e)); continue; }
        int frames = (int)(bytes_read / sizeof(int16_t) / 2);
        FollowBeatOut out = s_latest;
        for (int i = 0; i < frames; i++) out = follow_beat_push_sample(&s_detector, block[2 * i]);
        portENTER_CRITICAL(&s_lock);
        s_latest = out;
        portEXIT_CRITICAL(&s_lock);
    }
}

void follow_beat_io_start(void) {
    if (!audio_bus_ready()) {
        ESP_LOGE(TAG, "audio bus not ready -- follow beat disabled (call audio_bus_init() first)");
        return;
    }
    follow_beat_reset(&s_detector);
    memset(&s_latest, 0, sizeof(s_latest));

    esp_err_t e = i2s_channel_enable(audio_bus_rx());
    if (e != ESP_OK) { ESP_LOGE(TAG, "RX channel enable failed: %s -- follow beat disabled", esp_err_to_name(e)); return; }

    if (xTaskCreate(capture_task, "follow_beat", 4096, NULL, 4, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "capture task create failed -- follow beat disabled");
        return;
    }
    s_ready = true;
    ESP_LOGI(TAG, "follow beat ready");
}

bool follow_beat_io_ready(void) { return s_ready; }

FollowBeatOut follow_beat_io_status(void) {
    portENTER_CRITICAL(&s_lock);
    FollowBeatOut o = s_latest;
    portEXIT_CRITICAL(&s_lock);
    return o;
}
```

**Step 3: Commit**

```bash
git add KitchenSync/main/follow_beat_io.h KitchenSync/main/follow_beat_io.c
git commit -m "feat: follow_beat_io mic capture task (P4-020)"
```

---

### Task 10: Wire boot sequence + CMakeLists + web UI

**Files:**
- Modify: `KitchenSync/main/CMakeLists.txt`
- Modify: `KitchenSync/main/ks_main.c`
- Modify: `KitchenSync/main/ks_web.cpp`

**Step 1: Add new sources to the build**

In `KitchenSync/main/CMakeLists.txt`, add three lines to the `SRCS` list (these
are KitchenSync-local, not shared with X32Link, so plain filenames — put them
near `"metronome_audio.c"`):

```cmake
         "i2s_audio_bus.c"
         "follow_beat.c"
         "follow_beat_io.c"
```

**Step 2: Wire the boot sequence in `ks_main.c`**

Find where `metronome_audio_start()` is currently called (gated on
`g_cfg.metronome_enable`). Before that call, ensure `audio_bus_init()` runs
once if either audio feature is enabled:

```c
if (g_cfg.metronome_enable || g_cfg.follow_beat_enable) {
    audio_bus_init();
}
if (g_cfg.metronome_enable) {
    metronome_audio_start(g_cfg.metronome_volume, g_cfg.metronome_voice);
}
if (g_cfg.follow_beat_enable) {
    follow_beat_io_start();
}
```

Add `#include "i2s_audio_bus.h"` and `#include "follow_beat_io.h"` near the
other includes at the top of `ks_main.c`.

**Step 3: Web UI — status row + enable toggle**

In `KitchenSync/main/ks_web.cpp`'s `PAGE` HTML template, add a new toggle
group after the LED strip group (after the `</div>` that closes `%LEDCTL%`'s
`<div class="grp">`, before `<button class="write" ...>`):

```html
<div class="grp"><div class="frow head"><span class="cap">Follow Beat (Mic)</span>
<label class="sw"><input type="checkbox" name="follow_beat" value="1" %FBCHK%><span class="track"><span class="knob"></span></span><span class="swlbl"></span></label></div>
</div>
```

Add a read-only status row next to the other `<div class="row">` entries
(after the `Clock Out` row):

```html
<div class="row"><label>Follow Beat</label><span class="val" id="follow">off</span></div>
```

In `build_page()`, add the substitution:

```c
subst(h, "%FBCHK%", (s_cfg && s_cfg->follow_beat_enable) ? "checked" : "");
```

In the page's inline `<script>`, add a `follow` element ref next to the
others, and update `poll()` to render it:

```js
var followEl=document.getElementById('follow');
```

```js
if(typeof d.follow_valid!=='undefined'){
  followEl.textContent=d.follow_valid?(d.follow_bpm.toFixed(1)+' BPM'):'listening...';
}
```

(Add this inside the existing `poll()` function body, next to the `usbEl`/`minEl` lines.)

**Step 4: Verify the firmware builds**

```bash
source ~/esp/esp-idf/export.sh
cd /Users/edahl/Documents/GitHub/link-devices/.worktrees/p4-020-follow-beat/KitchenSync
idf.py set-target esp32p4   # only if not already configured for this target
idf.py build
```

Expected: build succeeds with no errors. Fix any compile errors before
proceeding — likely candidates: a missed `#include`, the `es8311_microphone_config`
call flagged in Task 7's `codec_init()` NOTE, or a stale `s_codec`/`i2s_init`
reference left in `metronome_audio.c` from Task 8.

**Step 5: Run the full host suite one more time (nothing here should have broken it)**

```bash
cd /Users/edahl/Documents/GitHub/link-devices/.worktrees/p4-020-follow-beat/test
make all
```

**Step 6: Commit**

```bash
cd /Users/edahl/Documents/GitHub/link-devices/.worktrees/p4-020-follow-beat
git add KitchenSync/main/CMakeLists.txt KitchenSync/main/ks_main.c KitchenSync/main/ks_web.cpp
git commit -m "feat: wire follow_beat into boot sequence + web UI (P4-020)"
```

---

### Task 11: Hardware validation (real board, real mic)

Not automatable — this is the actual proof the feature works, per the design
doc's testing section.

**Step 1: Flash the P4-NANO**

```bash
source ~/esp/esp-idf/export.sh
cd /Users/edahl/Documents/GitHub/link-devices/.worktrees/p4-020-follow-beat/KitchenSync
idf.py -p <serial-port> flash monitor
```

**Step 2: Enable Follow Beat**

Via the web UI (`http://<device-ip>/`), check "Follow Beat (Mic)", Write &
Reboot.

**Step 3: Confirm boot log**

Watch the serial monitor for `audio_bus: ...` and `follow beat ready` lines
with no `ESP_LOGE` errors. If `es8311_microphone_config`'s parameter meaning
(flagged in Task 7) was wrong for this board's mic wiring, this is where it'll
show up as silence/garbage rather than a build error — check the vendored
`es8311.h` docstring for that parameter if the BPM never goes valid despite
clear rhythmic audio.

**Step 4: Confirm detection**

Play a metronome/DAW click track at 90, 120, and 140 BPM near the device.
Watch `/status` (or the web UI's "Follow Beat" row) — confirm it converges to
each BPM within a few seconds and stays stable (matches the design doc's
"reported BPM converges within a few seconds and stays stable" criterion).

**Step 5: Confirm the metronome still works, and confirm they coexist**

Enable the metronome too (both features on at once). Confirm the speaker click
is still audible and undistorted, and Follow Beat still reports a sane BPM
while it plays — this is the actual scenario Task 1's design addendum exists
to make safe. If either is glitchy/silent with both enabled, the full-duplex
I2S sharing has a real problem — stop and re-consult the Embedded Firmware
Engineer with the specific symptom rather than guessing further.

**Step 6: Report back** (no commit — this is a manual validation record, not a
code change) with what was observed at each step, especially step 5.

---

## Summary of file changes

| File | Change |
|---|---|
| `docs/plans/2026-07-09-p4-020-follow-beat-design.md` | + addendum |
| `test/test_follow_beat.c` | new |
| `test/Makefile` | + `FOLLOWBEAT` var, `test_follow_beat` target |
| `KitchenSync/main/follow_beat.h` / `.c` | new (pure) |
| `test/test_ks_config.c` | + 2 tests |
| `KitchenSync/main/ks_config.h` / `.c` | + `follow_beat_enable` field |
| `test/test_ks_form.c` | + 2 tests |
| `KitchenSync/main/ks_form.c` | + checkbox pre-clear |
| `test/test_ks_status.c` | rewritten (3 new fields) |
| `KitchenSync/main/ks_status.h` / `.c` | + 3 status fields |
| `KitchenSync/main/i2s_audio_bus.h` / `.c` | new (impure, shared bus owner) |
| `KitchenSync/main/metronome_audio.c` | refactored to consume shared bus |
| `KitchenSync/main/follow_beat_io.h` / `.c` | new (impure, mic capture) |
| `KitchenSync/main/CMakeLists.txt` | + 3 sources |
| `KitchenSync/main/ks_main.c` | + boot wiring |
| `KitchenSync/main/ks_web.cpp` | + toggle, status row, JS, `/status` call site |
