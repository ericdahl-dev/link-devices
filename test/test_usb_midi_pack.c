// Host tests for the pure USB-MIDI 1.0 event-packet encoding (P4-005).
#include "unity.h"
#include "usb_midi_pack.h"

void setUp(void)    {}
void tearDown(void) {}

// 0xF8 timing clock on cable 0 -> CIN 0xF, status 0xF8, two zero data bytes.
void test_clock_on_cable0(void) {
    uint8_t p[4];
    usb_midi_pack_single(0, 0xF8, p);
    TEST_ASSERT_EQUAL_HEX8(0x0F, p[0]);
    TEST_ASSERT_EQUAL_HEX8(0xF8, p[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, p[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, p[3]);
}

// The cable number occupies the high nibble of byte0; CIN stays 0xF.
void test_cable_in_high_nibble(void) {
    uint8_t p[4];
    usb_midi_pack_single(1, 0xF8, p);
    TEST_ASSERT_EQUAL_HEX8(0x1F, p[0]);
    usb_midi_pack_single(3, 0xF8, p);
    TEST_ASSERT_EQUAL_HEX8(0x3F, p[0]);
}

// Works for the other single-byte transport messages, not just clock.
void test_start_stop_continue(void) {
    uint8_t p[4];
    usb_midi_pack_single(0, 0xFA, p);  // Start
    TEST_ASSERT_EQUAL_HEX8(0x0F, p[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFA, p[1]);
    usb_midi_pack_single(0, 0xFB, p);  // Continue
    TEST_ASSERT_EQUAL_HEX8(0xFB, p[1]);
    usb_midi_pack_single(0, 0xFC, p);  // Stop
    TEST_ASSERT_EQUAL_HEX8(0xFC, p[1]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_clock_on_cable0);
    RUN_TEST(test_cable_in_high_nibble);
    RUN_TEST(test_start_stop_continue);
    return UNITY_END();
}
