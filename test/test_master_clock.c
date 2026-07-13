// Host tests for the internal/tap tempo source + Link/internal arbiter (P4-040).
#include "unity.h"
#include "master_clock.h"
#include <string.h>

static MasterClock mc;

static LinkTimeline tl_of(int64_t mpb) {
    LinkTimeline tl; memset(&tl, 0, sizeof tl);
    tl.micros_per_beat = mpb;
    return tl;
}
static LinkGhostXForm locked_xform(void) {
    LinkGhostXForm x = { .intercept_us = 0, .valid = true };
    return x;
}
static LinkGhostXForm no_xform(void) {
    LinkGhostXForm x = { .intercept_us = 0, .valid = false };
    return x;
}

void setUp(void)    { master_clock_reset(&mc); }
void tearDown(void) {}

void test_set_bpm_computes_micros_per_beat(void) {
    master_clock_set_bpm(&mc, 120.0f);
    TEST_ASSERT_TRUE(mc.has_tempo);
    TEST_ASSERT_EQUAL_INT64(500000, mc.micros_per_beat);
}

// Two taps 500ms apart -> 120 bpm. The first tap alone can't compute an
// interval; has_tempo only flips true on the second.
void test_two_taps_compute_tempo(void) {
    master_clock_tap(&mc, 0);
    TEST_ASSERT_FALSE(mc.has_tempo);
    master_clock_tap(&mc, 500000);
    TEST_ASSERT_TRUE(mc.has_tempo);
    TEST_ASSERT_EQUAL_INT64(500000, mc.micros_per_beat);
}

// A tap more than MASTER_CLOCK_TAP_TIMEOUT_US after the previous one starts a
// fresh interval instead of computing a bogus multi-second "tempo" from it.
void test_stale_gap_does_not_compute_tempo(void) {
    master_clock_tap(&mc, 0);
    master_clock_tap(&mc, MASTER_CLOCK_TAP_TIMEOUT_US + 1);
    TEST_ASSERT_FALSE(mc.has_tempo);
}

// Peers present: the arbiter passes Link's values straight through, untouched,
// and reports on_internal=false regardless of any internal tempo state.
void test_arbiter_defers_to_link_when_peers_present(void) {
    MasterArbiterOut o = master_clock_arbiter(&mc, 1, true, tl_of(500000), locked_xform());
    TEST_ASSERT_TRUE(o.have_session);
    TEST_ASSERT_EQUAL_INT64(500000, o.tl.micros_per_beat);
    TEST_ASSERT_TRUE(o.xform.valid);
    TEST_ASSERT_FALSE(o.on_internal);
}

// Peers drop to 0 with a settled Link tempo: auto-seed on internal, continuous
// with what was just playing, and report on_internal so the UI can show it.
void test_arbiter_seeds_from_link_on_solo_edge(void) {
    master_clock_arbiter(&mc, 1, true, tl_of(500000), locked_xform());   // was following
    MasterArbiterOut o = master_clock_arbiter(&mc, 0, true, tl_of(500000), locked_xform());
    TEST_ASSERT_TRUE(o.have_session);
    TEST_ASSERT_EQUAL_INT64(500000, o.tl.micros_per_beat);
    TEST_ASSERT_FALSE(o.xform.valid);   // free-run, not phase-locked
    TEST_ASSERT_TRUE(o.on_internal);
}

// Never joined a session, never tapped: no tempo to originate. Matches
// today's behavior exactly (P4-039's territory, not a regression here).
void test_arbiter_inactive_when_never_joined_or_tapped(void) {
    MasterArbiterOut o = master_clock_arbiter(&mc, 0, false, tl_of(0), no_xform());
    TEST_ASSERT_FALSE(o.have_session);
    TEST_ASSERT_FALSE(o.on_internal);
}

// A live tap taken while already solo must not be clobbered by a later tick's
// arbiter call (the seed only applies on the peers>0->0 edge, not every tick).
void test_arbiter_does_not_clobber_live_tap_while_solo(void) {
    master_clock_arbiter(&mc, 0, true, tl_of(500000), locked_xform());   // solo edge, seeds 120bpm
    master_clock_tap(&mc, 0);
    master_clock_tap(&mc, 400000);   // 150 bpm
    MasterArbiterOut o = master_clock_arbiter(&mc, 0, true, tl_of(500000), locked_xform());
    TEST_ASSERT_EQUAL_INT64(400000, o.tl.micros_per_beat);
}

// A peer reappears while solo-promoted: defer back to Link immediately, no
// merge logic (we never broadcast, so there is no competing session).
void test_arbiter_rejoin_defers_to_link(void) {
    master_clock_arbiter(&mc, 0, true, tl_of(500000), locked_xform());   // go solo
    master_clock_tap(&mc, 0);
    master_clock_tap(&mc, 400000);                                       // retune while alone
    MasterArbiterOut o = master_clock_arbiter(&mc, 1, true, tl_of(500000), locked_xform());
    TEST_ASSERT_EQUAL_INT64(500000, o.tl.micros_per_beat);   // Link's value, not the tap
    TEST_ASSERT_FALSE(o.on_internal);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_set_bpm_computes_micros_per_beat);
    RUN_TEST(test_two_taps_compute_tempo);
    RUN_TEST(test_stale_gap_does_not_compute_tempo);
    RUN_TEST(test_arbiter_defers_to_link_when_peers_present);
    RUN_TEST(test_arbiter_seeds_from_link_on_solo_edge);
    RUN_TEST(test_arbiter_inactive_when_never_joined_or_tapped);
    RUN_TEST(test_arbiter_does_not_clobber_live_tap_while_solo);
    RUN_TEST(test_arbiter_rejoin_defers_to_link);
    return UNITY_END();
}
