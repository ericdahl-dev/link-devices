// Host tests for the pure KitchenSync /status JSON builder (P4-007).
#include "unity.h"
#include "ks_status.h"
#include <string.h>

void setUp(void)    {}
void tearDown(void) {}

// All fields present; usb serializes as a real JSON bool. `min` is the detected
// MIDI-clock-IN tempo (P4-011).
void test_all_fields_present(void) {
    char b[160];
    ks_status_json(b, sizeof(b), 132.0f, 120.5f, 1, true, 583, "2.1.0");
    TEST_ASSERT_NOT_NULL(strstr(b, "\"bpm\":132.0"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"min\":120.5"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"peers\":1"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"usb\":true"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"tx\":583"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"fw\":\"2.1.0\""));
}

// usb false serializes as the bool false, not 0/"false".
void test_usb_false_is_json_bool(void) {
    char b[160];
    ks_status_json(b, sizeof(b), 0.0f, 0.0f, 0, false, 0, "2.1.0");
    TEST_ASSERT_NOT_NULL(strstr(b, "\"usb\":false"));
    TEST_ASSERT_NULL(strstr(b, "true"));
}

// Return value mirrors snprintf's contract — bytes that would've been written,
// so a too-small buffer is detectable by the caller (same test as the
// web_status_json twin).
void test_return_value_is_snprintf_style_length(void) {
    char b[160];
    int n = ks_status_json(b, sizeof(b), 132.0f, 120.5f, 1, true, 583, "2.1.0");
    TEST_ASSERT_EQUAL_INT((int)strlen(b), n);

    char tiny[4];
    int n2 = ks_status_json(tiny, sizeof(tiny), 132.0f, 120.5f, 1, true, 583, "2.1.0");
    TEST_ASSERT_TRUE(n2 > (int)sizeof(tiny) - 1);  // truncated but length still reported
}

// LNK-038: the fw string is caller-supplied (pure builder stays version-agnostic).
void test_fw_string_passes_through(void) {
    char b[160];
    ks_status_json(b, sizeof(b), 120.0f, 0.0f, 0, false, 0, "9.9.9-rc1");
    TEST_ASSERT_NOT_NULL(strstr(b, "\"fw\":\"9.9.9-rc1\""));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_all_fields_present);
    RUN_TEST(test_usb_false_is_json_bool);
    RUN_TEST(test_return_value_is_snprintf_style_length);
    RUN_TEST(test_fw_string_passes_through);
    return UNITY_END();
}
