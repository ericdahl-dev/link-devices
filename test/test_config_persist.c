// Host tests for the ARC-022 debounced config write-through policy.
//
// The bug this exists to kill: /live applied a setting to the running config and
// never wrote NVS, so every live-safe field -- metronome voice, LED colours, and
// the whole clock[] array including follow_link -- silently reverted on reboot.
// Writing on every /live POST is not the fix: config is ONE nvs blob, and a
// slider drag emits dozens of POSTs, so that would blob-write flash tens of times
// per gesture.
//
// So a live edit marks the config dirty and the blob is written once the edits
// settle. That debounce is the only part of the fix with real logic in it, which
// is why it is the part that is pure and tested. Time is an INPUT: the test owns
// the clock, so the timing behaviour is provable with no device, no flash, and no
// power cycle.
#include "unity.h"
#include "config_persist.h"

void setUp(void) {}
void tearDown(void) {}

// A fresh (reset) tracker is not dirty and never asks for a write. Asserted
// explicitly because callers stack-allocate: clock_ticker.dropped read stack
// garbage for exactly this reason, and reset() is the only thing standing
// between a struct declared on the stack and a spurious flash write at boot.
void test_reset_is_clean(void) {
    ConfigPersist p;
    config_persist_reset(&p);
    TEST_ASSERT_FALSE(p.dirty);
    TEST_ASSERT_FALSE(config_persist_due(&p, 0));
    TEST_ASSERT_FALSE(config_persist_due(&p, 100000));   // still nothing, much later
}

// One edit: nothing is written while the quiet window is still running, and the
// write lands once it elapses. (The user moves one control and walks away.)
void test_single_edit_writes_after_quiet_window(void) {
    ConfigPersist p;
    config_persist_reset(&p);

    config_persist_mark(&p, 1000);
    TEST_ASSERT_FALSE(config_persist_due(&p, 1000));                                 // same instant
    TEST_ASSERT_FALSE(config_persist_due(&p, 1000 + CONFIG_PERSIST_QUIET_MS - 1));   // one ms early
    TEST_ASSERT_TRUE (config_persist_due(&p, 1000 + CONFIG_PERSIST_QUIET_MS));       // due
}

// THE slider-drag case, and the reason debounce exists at all: a burst of edits
// inside the quiet window produces EXACTLY ONE write, and it lands after the LAST
// edit -- not the first. Each new edit pushes the deadline out.
void test_burst_of_edits_produces_one_write(void) {
    ConfigPersist p;
    config_persist_reset(&p);

    int writes = 0;
    uint32_t t = 5000;
    for (int i = 0; i < 40; i++) {          // 40 POSTs, 50 ms apart: one drag of a slider
        config_persist_mark(&p, t);
        if (config_persist_due(&p, t)) writes++;
        t += 50;
    }
    TEST_ASSERT_EQUAL_INT(0, writes);       // nothing written DURING the drag

    // The drag ends. Poll every 50 ms for a good while afterwards.
    uint32_t last_edit = t - 50;
    for (int i = 0; i < 200; i++) {
        if (config_persist_due(&p, t)) writes++;
        t += 50;
    }
    TEST_ASSERT_EQUAL_INT(1, writes);       // exactly one, and only one

    // ...and it landed on the quiet window measured from the LAST edit, not the first.
    config_persist_reset(&p);
    config_persist_mark(&p, last_edit);
    TEST_ASSERT_TRUE(config_persist_due(&p, last_edit + CONFIG_PERSIST_QUIET_MS));
}

// due() is true exactly ONCE per settled burst: it clears the dirty flag, so the
// status task polling it every second cannot re-write the same blob every pass.
void test_due_fires_once_then_goes_quiet(void) {
    ConfigPersist p;
    config_persist_reset(&p);

    config_persist_mark(&p, 1000);
    TEST_ASSERT_TRUE (config_persist_due(&p, 1000 + CONFIG_PERSIST_QUIET_MS));
    TEST_ASSERT_FALSE(config_persist_due(&p, 1000 + CONFIG_PERSIST_QUIET_MS));       // same instant
    TEST_ASSERT_FALSE(config_persist_due(&p, 1000 + CONFIG_PERSIST_QUIET_MS + 1));
    TEST_ASSERT_FALSE(config_persist_due(&p, 999999));                               // stays quiet

    // A NEW edit after the write starts a fresh cycle -- one write per burst, always.
    config_persist_mark(&p, 1000000);
    TEST_ASSERT_FALSE(config_persist_due(&p, 1000000));
    TEST_ASSERT_TRUE (config_persist_due(&p, 1000000 + CONFIG_PERSIST_QUIET_MS));
}

// Debounce DEFERS a write; it must not STARVE one. A stream of edits that never
// leaves a quiet gap still gets written at the maximum-deferral bound, measured
// from the first edit of the burst.
void test_continuous_edits_still_write_at_max_deferral(void) {
    ConfigPersist p;
    config_persist_reset(&p);

    const uint32_t t0 = 200000;
    uint32_t first_write_at = 0;
    int writes = 0;

    // Edit every 100 ms forever -- the quiet window never elapses on its own.
    for (uint32_t t = t0; t <= t0 + CONFIG_PERSIST_MAX_DEFER_MS; t += 100) {
        config_persist_mark(&p, t);
        if (config_persist_due(&p, t)) {
            if (!writes) first_write_at = t;
            writes++;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, writes);
    TEST_ASSERT_EQUAL_UINT32(t0 + CONFIG_PERSIST_MAX_DEFER_MS, first_write_at);
}

// millis() rolls over at 2^32 ms (~49 days) and the device is meant to live on a
// shelf between gigs. A burst that straddles the wrap must still settle: the
// deltas are unsigned, so this works -- comparing timestamps directly would not.
void test_survives_millis_rollover(void) {
    ConfigPersist p;
    config_persist_reset(&p);

    const uint32_t before_wrap = 0xFFFFFF00u;          // 256 ms short of the wrap
    config_persist_mark(&p, before_wrap);

    TEST_ASSERT_FALSE(config_persist_due(&p, 0x00000010u));   // wrapped, but only ~272 ms of quiet
    TEST_ASSERT_TRUE (config_persist_due(&p, (uint32_t)(before_wrap + CONFIG_PERSIST_QUIET_MS)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_reset_is_clean);
    RUN_TEST(test_single_edit_writes_after_quiet_window);
    RUN_TEST(test_burst_of_edits_produces_one_write);
    RUN_TEST(test_due_fires_once_then_goes_quiet);
    RUN_TEST(test_continuous_edits_still_write_at_max_deferral);
    RUN_TEST(test_survives_millis_rollover);
    return UNITY_END();
}
