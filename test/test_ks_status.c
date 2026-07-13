// Host tests for the pure KitchenSync /status JSON builder (P4-007, extended
// P4-020 with the mic tempo-follow fields, ESP-011 launch, + transport state).
#include "unity.h"
#include "ks_status.h"
#include <string.h>

static const int kNoLaunch[4] = {0,0,0,0};

void setUp(void)    {}
void tearDown(void) {}

// All fields present; usb/follow_enabled/follow_valid serialize as real JSON bools.
void test_all_fields_present(void) {
    char b[300];
    ks_status_json(b, sizeof(b), 132.0f, 120.5f, 1, true, 583, "2.1.0", true, 128.3f, 3.1f, true, kNoLaunch, false, false, NULL, NULL);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"bpm\":132.0"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"min\":120.5"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"peers\":1"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"usb\":true"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"tx\":583"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"fw\":\"2.1.0\""));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow_enabled\":true"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow_bpm\":128.3"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow_confidence\":3.1"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow_valid\":true"));
}

void test_usb_false_is_json_bool(void) {
    char b[300];
    ks_status_json(b, sizeof(b), 0.0f, 0.0f, 0, false, 0, "2.1.0", false, 0.0f, 0.0f, false, kNoLaunch, false, false, NULL, NULL);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"usb\":false"));
}

// follow_valid false serializes as the bool false, not 0/"false".
void test_follow_valid_false_is_json_bool(void) {
    char b[300];
    ks_status_json(b, sizeof(b), 0.0f, 0.0f, 0, false, 0, "2.1.0", false, 0.0f, 0.0f, false, kNoLaunch, false, false, NULL, NULL);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow_valid\":false"));
}

// follow_enabled is distinct from follow_valid: the feature can be enabled
// but not yet confident (valid=false while enabled=true) -- the web UI needs
// to tell "off" apart from "listening", so these two fields must vary
// independently, not just mirror each other.
void test_follow_enabled_independent_of_valid(void) {
    char b[300];
    ks_status_json(b, sizeof(b), 0.0f, 0.0f, 0, false, 0, "2.1.0", true, 0.0f, 0.0f, false, kNoLaunch, false, false, NULL, NULL);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow_enabled\":true"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow_valid\":false"));
}

void test_return_value_is_snprintf_style_length(void) {
    char b[300];
    int n = ks_status_json(b, sizeof(b), 132.0f, 120.5f, 1, true, 583, "2.1.0", true, 128.3f, 3.1f, true, kNoLaunch, false, false, NULL, NULL);
    TEST_ASSERT_EQUAL_INT((int)strlen(b), n);

    char tiny[4];
    int n2 = ks_status_json(tiny, sizeof(tiny), 132.0f, 120.5f, 1, true, 583, "2.1.0", true, 128.3f, 3.1f, true, kNoLaunch, false, false, NULL, NULL);
    TEST_ASSERT_TRUE(n2 > (int)sizeof(tiny) - 1);  // truncated but length still reported
}

void test_fw_string_passes_through(void) {
    char b[300];
    ks_status_json(b, sizeof(b), 120.0f, 0.0f, 0, false, 0, "9.9.9-rc1", false, 0.0f, 0.0f, false, kNoLaunch, false, false, NULL, NULL);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"fw\":\"9.9.9-rc1\""));
}

// ESP-011: per-output launch state (0 stopped / 1 armed / 2 running) so the UI
// can show "starting on next bar..." instead of a dead button.
void test_launch_state_array_present(void) {
    char b[300];
    const int ls[4] = { 2, 1, 0, 0 };
    ks_status_json(b, sizeof(b), 132.0f, 0.0f, 1, true, 0, "2.1.0", false, 0.0f, 0.0f, false, ls, false, false, NULL, NULL);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"launch\":[2,1,0,0]"));
}

// Transport state: the session's real playing flag and whether Link owns
// transport, so the UI can reflect play/stop and grey the manual buttons.
void test_transport_state_fields(void) {
    char b[300];
    ks_status_json(b, sizeof(b), 126.0f, 0.0f, 2, false, 0, "2.1.0", false, 0.0f, 0.0f, false, kNoLaunch, true, true, NULL, NULL);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"playing\":true"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"link_owns\":true"));
}

// ...and both serialize as real JSON bools when false (not 0), independently.
void test_transport_state_false_bools(void) {
    char b[300];
    ks_status_json(b, sizeof(b), 126.0f, 0.0f, 0, false, 0, "2.1.0", false, 0.0f, 0.0f, false, kNoLaunch, false, false, NULL, NULL);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"playing\":false"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"link_owns\":false"));
}

// P4-038: the 1 ms clock task's health has to reach /status. KitchenSync already HAD
// this probe and threw it away into a once-a-second serial log, so a 766 ms clock stall
// (measured on the analyzer) left no trace unless someone had serial attached at that
// exact moment. Published, or it may as well not exist.
void test_tick_health_block_published(void) {
    char b[420];
    WebTickHealth t = { .dropped = 35, .bursts = 2, .max_gap_us = 766116, .max_work_us = 480,
                        .overruns = 3, .w_beats = 62, .w_clock = 5038, .core = 1, .reprimes = 7 };
    ks_status_json(b, sizeof(b), 120.0f, 0.0f, 1, true, 900, "2.2.0", false, 0.0f, 0.0f, false,
                   kNoLaunch, false, false, &t, NULL);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"drop\":35"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"burst\":2"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"gap\":766116"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"work\":480"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"over\":3"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"core\":1"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"w_beats\":62"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"w_clock\":5038"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"reprime\":7"));
}

// NULL tick => the block is ABSENT, not zero-filled. A row of zeroes reads as
// "measured, all clean" -- which is exactly the lie this probe exists to prevent.
void test_no_tick_health_omits_block_rather_than_zero_filling(void) {
    char b[420];
    ks_status_json(b, sizeof(b), 120.0f, 0.0f, 1, true, 900, "2.2.0", false, 0.0f, 0.0f, false,
                   kNoLaunch, false, false, NULL, NULL);
    TEST_ASSERT_NULL(strstr(b, "\"gap\""));
    TEST_ASSERT_NULL(strstr(b, "\"drop\""));
    TEST_ASSERT_NULL(strstr(b, "\"core\""));
    TEST_ASSERT_NULL(strstr(b, "\"gap\":0"));      // the zero-filled row we must never emit
    TEST_ASSERT_EQUAL_CHAR('}', b[strlen(b) - 1]); // still closes
}

// The tick block made the payload ~90 bytes longer. Truncation must still be
// DETECTABLE (snprintf semantics: return what it WOULD have written), or ks_web
// silently serves half a JSON object and the UI just stops updating.
void test_truncation_still_detectable_with_tick_block(void) {
    char tiny[64];
    WebTickHealth t = { .dropped = 35, .bursts = 2, .max_gap_us = 766116, .max_work_us = 480,
                        .overruns = 3, .w_beats = 62, .w_clock = 5038, .core = 1 };
    int n = ks_status_json(tiny, sizeof(tiny), 120.0f, 0.0f, 1, true, 900, "2.2.0",
                           false, 0.0f, 0.0f, false, kNoLaunch, false, false, &t, NULL);
    TEST_ASSERT_GREATER_THAN_INT((int)sizeof(tiny), n);   // caller can see it did not fit
    TEST_ASSERT_EQUAL_CHAR('\0', tiny[sizeof(tiny) - 1]); // and we never ran off the end
}

// P4-038: the origin-step gauge. This is the number that moves the BAR LINE: the
// GhostXForm is only an origin and a commit STEPS it, with no slew and no clamp.
void test_phase_health_block_published(void) {
    char b[520];
    LinkPhaseHealth ph = { .commits = 31, .last_step_us = 118000, .max_step_us = 185000,
                           .rtt_min_us = 4000, .rtt_max_us = 214000 };
    ks_status_json(b, sizeof(b), 120.0f, 0.0f, 1, true, 900, "2.2.0", false, 0.0f, 0.0f, false,
                   kNoLaunch, false, false, NULL, &ph);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"xf\":31"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"xf_step\":118000"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"xf_max\":185000"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"rtt_min\":4000"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"rtt_max\":214000"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_phase_health_block_published);
    RUN_TEST(test_tick_health_block_published);
    RUN_TEST(test_no_tick_health_omits_block_rather_than_zero_filling);
    RUN_TEST(test_truncation_still_detectable_with_tick_block);
    RUN_TEST(test_launch_state_array_present);
    RUN_TEST(test_all_fields_present);
    RUN_TEST(test_usb_false_is_json_bool);
    RUN_TEST(test_follow_valid_false_is_json_bool);
    RUN_TEST(test_follow_enabled_independent_of_valid);
    RUN_TEST(test_return_value_is_snprintf_style_length);
    RUN_TEST(test_fw_string_passes_through);
    RUN_TEST(test_transport_state_fields);
    RUN_TEST(test_transport_state_false_bools);
    return UNITY_END();
}
