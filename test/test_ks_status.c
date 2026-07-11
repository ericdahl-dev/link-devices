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
    ks_status_json(b, sizeof(b), 132.0f, 120.5f, 1, true, 583, "2.1.0", true, 128.3f, 3.1f, true, kNoLaunch, false, false);
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
    ks_status_json(b, sizeof(b), 0.0f, 0.0f, 0, false, 0, "2.1.0", false, 0.0f, 0.0f, false, kNoLaunch, false, false);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"usb\":false"));
}

// follow_valid false serializes as the bool false, not 0/"false".
void test_follow_valid_false_is_json_bool(void) {
    char b[300];
    ks_status_json(b, sizeof(b), 0.0f, 0.0f, 0, false, 0, "2.1.0", false, 0.0f, 0.0f, false, kNoLaunch, false, false);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow_valid\":false"));
}

// follow_enabled is distinct from follow_valid: the feature can be enabled
// but not yet confident (valid=false while enabled=true) -- the web UI needs
// to tell "off" apart from "listening", so these two fields must vary
// independently, not just mirror each other.
void test_follow_enabled_independent_of_valid(void) {
    char b[300];
    ks_status_json(b, sizeof(b), 0.0f, 0.0f, 0, false, 0, "2.1.0", true, 0.0f, 0.0f, false, kNoLaunch, false, false);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow_enabled\":true"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow_valid\":false"));
}

void test_return_value_is_snprintf_style_length(void) {
    char b[300];
    int n = ks_status_json(b, sizeof(b), 132.0f, 120.5f, 1, true, 583, "2.1.0", true, 128.3f, 3.1f, true, kNoLaunch, false, false);
    TEST_ASSERT_EQUAL_INT((int)strlen(b), n);

    char tiny[4];
    int n2 = ks_status_json(tiny, sizeof(tiny), 132.0f, 120.5f, 1, true, 583, "2.1.0", true, 128.3f, 3.1f, true, kNoLaunch, false, false);
    TEST_ASSERT_TRUE(n2 > (int)sizeof(tiny) - 1);  // truncated but length still reported
}

void test_fw_string_passes_through(void) {
    char b[300];
    ks_status_json(b, sizeof(b), 120.0f, 0.0f, 0, false, 0, "9.9.9-rc1", false, 0.0f, 0.0f, false, kNoLaunch, false, false);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"fw\":\"9.9.9-rc1\""));
}

// ESP-011: per-output launch state (0 stopped / 1 armed / 2 running) so the UI
// can show "starting on next bar..." instead of a dead button.
void test_launch_state_array_present(void) {
    char b[300];
    const int ls[4] = { 2, 1, 0, 0 };
    ks_status_json(b, sizeof(b), 132.0f, 0.0f, 1, true, 0, "2.1.0", false, 0.0f, 0.0f, false, ls, false, false);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"launch\":[2,1,0,0]"));
}

// Transport state: the session's real playing flag and whether Link owns
// transport, so the UI can reflect play/stop and grey the manual buttons.
void test_transport_state_fields(void) {
    char b[300];
    ks_status_json(b, sizeof(b), 126.0f, 0.0f, 2, false, 0, "2.1.0", false, 0.0f, 0.0f, false, kNoLaunch, true, true);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"playing\":true"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"link_owns\":true"));
}

// ...and both serialize as real JSON bools when false (not 0), independently.
void test_transport_state_false_bools(void) {
    char b[300];
    ks_status_json(b, sizeof(b), 126.0f, 0.0f, 0, false, 0, "2.1.0", false, 0.0f, 0.0f, false, kNoLaunch, false, false);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"playing\":false"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"link_owns\":false"));
}

int main(void) {
    UNITY_BEGIN();
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
