// Host tests for the pure P4Hub /status JSON builder (P4-007).
#include "unity.h"
#include "p4hub_status.h"
#include <string.h>

void setUp(void)    {}
void tearDown(void) {}

// All fields present; usb serializes as a real JSON bool. `min` is the detected
// MIDI-clock-IN tempo (P4-011).
void test_all_fields_present(void) {
    char b[128];
    p4hub_status_json(b, sizeof(b), 132.0f, 120.5f, 1, true, 583);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"bpm\":132.0"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"min\":120.5"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"peers\":1"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"usb\":true"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"tx\":583"));
}

// usb false serializes as the bool false, not 0/"false".
void test_usb_false_is_json_bool(void) {
    char b[128];
    p4hub_status_json(b, sizeof(b), 0.0f, 0.0f, 0, false, 0);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"usb\":false"));
    TEST_ASSERT_NULL(strstr(b, "true"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_all_fields_present);
    RUN_TEST(test_usb_false_is_json_bool);
    return UNITY_END();
}
