// Host tests for the pure beat-source selector (ARC-007). beat_source owns the
// policy that used to live inline in clock_out_task: pick the phase-locked
// session beat vs the free-running local accumulator, and tell the caller when a
// basis switch (or session loss) means it must re-prime its tick/click grids.
#include "unity.h"
#include "beat_source.h"
#include <string.h>
#include <math.h>

// This repo builds Unity without double asserts; compare beats the way the
// sibling beat_clock tests do.
#define ASSERT_BEATS(expected, actual) TEST_ASSERT_TRUE(fabs((actual) - (expected)) < 1e-9)

// 120 BPM: one beat = 500 ms. Free-run beat_clock returns 0 on its first
// (priming) advance, then integrates from there.
#define MPB 500000

static BeatSource s;
static LinkTimeline tl;

void setUp(void) {
    beat_source_reset(&s);
    memset(&tl, 0, sizeof(tl));
    tl.micros_per_beat = MPB;
}
void tearDown(void) {}

static LinkGhostXForm locked_xform(int64_t intercept) {
    LinkGhostXForm x = { .intercept_us = intercept, .valid = true };
    return x;
}
static LinkGhostXForm no_xform(void) {
    LinkGhostXForm x = { .intercept_us = 0, .valid = false };
    return x;
}

// Before any session there is no beat and nothing to re-prime.
void test_idle_is_inactive(void) {
    BeatSourceOut o = beat_source_step(&s, false, no_xform(), tl, 0);
    TEST_ASSERT_FALSE(o.active);
    TEST_ASSERT_FALSE(o.reprime);
}

// Acquiring a session with no committed xform runs free: beat 0 on the priming
// step, then integrates local time. First acquisition doesn't re-prime.
void test_free_run_from_local_clock(void) {
    BeatSourceOut a = beat_source_step(&s, true, no_xform(), tl, 0);
    TEST_ASSERT_TRUE(a.active);
    TEST_ASSERT_FALSE(a.locked);
    TEST_ASSERT_FALSE(a.reprime);
    ASSERT_BEATS(0.0, a.beats);

    BeatSourceOut b = beat_source_step(&s, true, no_xform(), tl, MPB);   // +1 beat
    ASSERT_BEATS(1.0, b.beats);
}

// A committed xform reads the true SESSION beat via link_phase, and re-primes on
// the free->locked switch so the grid realigns to the session downbeat.
void test_lock_uses_session_phase_and_reprimes(void) {
    beat_source_step(&s, true, no_xform(), tl, 0);            // start free
    LinkGhostXForm x = locked_xform(1000000);
    BeatSourceOut o = beat_source_step(&s, true, x, tl, MPB);
    TEST_ASSERT_TRUE(o.active);
    TEST_ASSERT_TRUE(o.locked);
    TEST_ASSERT_TRUE(o.reprime);                             // basis switched
    int64_t ghost = link_ghost_xform_host_to_ghost(x, MPB);
    ASSERT_BEATS(link_phase_beats_now(tl, ghost), o.beats);
}

// First acquisition straight into a locked session still re-primes (the grid was
// never aligned to the session), matching the old `locked != phase_locked` edge.
void test_first_acquire_locked_reprimes(void) {
    BeatSourceOut o = beat_source_step(&s, true, locked_xform(0), tl, 0);
    TEST_ASSERT_TRUE(o.locked);
    TEST_ASSERT_TRUE(o.reprime);
}

// Staying locked across steps does not keep re-priming.
void test_steady_lock_no_reprime(void) {
    beat_source_step(&s, true, locked_xform(0), tl, 0);       // reprimes (switch)
    BeatSourceOut o = beat_source_step(&s, true, locked_xform(0), tl, MPB);
    TEST_ASSERT_FALSE(o.reprime);
}

// Dropping from locked back to free re-primes and restarts the free accumulator
// from 0 (clean realign, no stale-clock jump).
void test_lock_drop_to_free_reprimes_and_restarts(void) {
    beat_source_step(&s, true, locked_xform(0), tl, 0);
    BeatSourceOut o = beat_source_step(&s, true, no_xform(), tl, MPB);
    TEST_ASSERT_FALSE(o.locked);
    TEST_ASSERT_TRUE(o.reprime);
    ASSERT_BEATS(0.0, o.beats);                  // free restarts at 0
}

// Losing the session re-primes exactly once (the caller resets its grids +
// transport on that edge), then stays quiet while idle.
void test_session_loss_reprimes_once(void) {
    beat_source_step(&s, true, no_xform(), tl, 0);           // running
    BeatSourceOut loss = beat_source_step(&s, false, no_xform(), tl, MPB);
    TEST_ASSERT_FALSE(loss.active);
    TEST_ASSERT_TRUE(loss.reprime);
    BeatSourceOut still = beat_source_step(&s, false, no_xform(), tl, 2 * MPB);
    TEST_ASSERT_FALSE(still.reprime);
}

// After a loss, re-acquiring free starts the accumulator fresh from 0.
void test_reacquire_after_loss_restarts_free(void) {
    beat_source_step(&s, true, no_xform(), tl, 0);
    beat_source_step(&s, true, no_xform(), tl, MPB);          // beat 1
    beat_source_step(&s, false, no_xform(), tl, 2 * MPB);     // loss
    BeatSourceOut o = beat_source_step(&s, true, no_xform(), tl, 3 * MPB);
    ASSERT_BEATS(0.0, o.beats);
}




/* ---- ESP-027: a backwards beat step is a re-origin, not physics ---------- */

// THE BUG: a fresh GhostXForm commits, `locked` stays true (no basis switch, so the
// old rule fires nothing), and the beat position steps BACKWARDS. Measured on the wire:
// 276 beats backwards on a Link rejoin -- and clock_ticker, primed at the old position,
// emitted SILENCE for over two minutes waiting for beats to climb back, while /status
// reported sync:1 peers:1. Time does not run backwards; a backstep IS a re-origin.
void test_backwards_beat_step_reprimes(void) {
    // locked, beats advancing from a session whose origin is far in the past
    BeatSourceOut o = beat_source_step(&s, true, locked_xform(0), tl, 100000000);
    TEST_ASSERT_TRUE(o.locked);
    double before = o.beats;

    // re-origin: SAME basis (still locked), but the intercept moves so `now` maps to a
    // much earlier beat (ghost = host + intercept, so a negative intercept steps back).
    // The old rule sees locked==was_locked and re-primes nothing.
    o = beat_source_step(&s, true, locked_xform(-90000000), tl, 100000000);
    TEST_ASSERT_TRUE(o.locked);                 // basis did NOT switch
    TEST_ASSERT_TRUE(o.beats < before - 1.0);   // and beats went backwards
    TEST_ASSERT_TRUE(o.reprime);                // ...so THIS must fire
}

// An ordinary sub-millisecond xform refinement must NOT re-prime -- resetting the grid on
// every tiny correction would be its own bug (the grid would never settle).
void test_tiny_backstep_does_not_reprime(void) {
    beat_source_step(&s, true, locked_xform(0), tl, 100000000);
    // 10 ms of intercept = 0.02 beat at 120 BPM, well under BEAT_SOURCE_BACKSTEP_BEATS
    BeatSourceOut o = beat_source_step(&s, true, locked_xform(-10000), tl, 100001000);
    TEST_ASSERT_FALSE(o.reprime);
}

// Forward motion is the normal case and must never re-prime.
void test_forward_motion_does_not_reprime(void) {
    beat_source_step(&s, true, locked_xform(0), tl, 100000000);
    for (int i = 1; i <= 20; i++) {
        BeatSourceOut o = beat_source_step(&s, true, locked_xform(0), tl, 100000000 + i * 100000);
        TEST_ASSERT_FALSE(o.reprime);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_is_inactive);
    RUN_TEST(test_free_run_from_local_clock);
    RUN_TEST(test_lock_uses_session_phase_and_reprimes);
    RUN_TEST(test_first_acquire_locked_reprimes);
    RUN_TEST(test_steady_lock_no_reprime);
    RUN_TEST(test_lock_drop_to_free_reprimes_and_restarts);
    RUN_TEST(test_session_loss_reprimes_once);
    RUN_TEST(test_reacquire_after_loss_restarts_free);
    RUN_TEST(test_backwards_beat_step_reprimes);
    RUN_TEST(test_tiny_backstep_does_not_reprime);
    RUN_TEST(test_forward_motion_does_not_reprime);
    return UNITY_END();
}
