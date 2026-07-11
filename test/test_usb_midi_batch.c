// Host tests for the pure USB-MIDI event batcher (P4-034).
//
// The bug this exists to kill: usb_midi_host_send() has ONE in-flight bulk transfer
// and drops anything submitted while it's busy. ks_main called it once per output,
// back-to-back in the same 1 ms tick, so with 4 outputs enabled ~3 of every 4 clock
// packets were silently thrown away -- only the first output ever reached USB gear.
// Measured on the analyzer: two outputs on the same cable produced a 1.004x pulse
// ratio instead of 2.0x.
//
// Fix: pack the tick's events into ONE transfer. A USB-MIDI event is 4 bytes and a
// bulk transfer carries 64, so 16 events fit -- four outputs is not close to the limit.
#include "unity.h"
#include "usb_midi_batch.h"
#include <string.h>

static UsbMidiBatch b;
void setUp(void)    { usb_midi_batch_reset(&b); b.dropped = 0; }
void tearDown(void) {}

// Tracer bullet: FOUR outputs emit in one tick and ALL FOUR survive. This is the
// exact case that was broken on the wire.
void test_four_outputs_all_fit_in_one_transfer(void) {
    const uint8_t p0[4] = {0x0F, 0xF8, 0, 0};   // cable 0 clock
    const uint8_t p1[4] = {0x1F, 0xF8, 0, 0};   // cable 1
    const uint8_t p2[4] = {0x2F, 0xF8, 0, 0};   // cable 2
    const uint8_t p3[4] = {0x3F, 0xF8, 0, 0};   // cable 3

    TEST_ASSERT_TRUE(usb_midi_batch_add(&b, p0));
    TEST_ASSERT_TRUE(usb_midi_batch_add(&b, p1));
    TEST_ASSERT_TRUE(usb_midi_batch_add(&b, p2));
    TEST_ASSERT_TRUE(usb_midi_batch_add(&b, p3));

    TEST_ASSERT_EQUAL_INT(16, b.len);          // one transfer, four events
    TEST_ASSERT_EQUAL_UINT32(0, b.dropped);    // nothing thrown away
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p0, b.buf + 0,  4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p1, b.buf + 4,  4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p2, b.buf + 8,  4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p3, b.buf + 12, 4);
}

// A full transfer refuses the next event and SAYS SO. The bug being fixed dropped
// packets silently; an overflow here must be loud (false + a counted drop) and must
// not corrupt the bytes already staged.
void test_full_transfer_refuses_and_counts(void) {
    const uint8_t p[4] = {0x0F, 0xF8, 0, 0};
    for (int i = 0; i < 16; i++) TEST_ASSERT_TRUE(usb_midi_batch_add(&b, p));  // 16 x 4 = 64
    TEST_ASSERT_EQUAL_INT(64, b.len);
    TEST_ASSERT_EQUAL_UINT32(0, b.dropped);

    const uint8_t over[4] = {0x1F, 0xFA, 0, 0};
    TEST_ASSERT_FALSE(usb_midi_batch_add(&b, over));   // 17th does not fit
    TEST_ASSERT_EQUAL_INT(64, b.len);                  // staged bytes untouched
    TEST_ASSERT_EQUAL_UINT32(1, b.dropped);            // and it is COUNTED
}

// reset clears the staged bytes but keeps `dropped` — it is a running health counter,
// so a drop can never be erased by the next tick.
void test_reset_clears_bytes_but_keeps_drop_count(void) {
    const uint8_t p[4] = {0x0F, 0xF8, 0, 0};
    for (int i = 0; i < 17; i++) usb_midi_batch_add(&b, p);   // 16 fit, 1 dropped
    TEST_ASSERT_EQUAL_UINT32(1, b.dropped);

    usb_midi_batch_reset(&b);
    TEST_ASSERT_EQUAL_INT(0, b.len);
    TEST_ASSERT_EQUAL_UINT32(1, b.dropped);   // survives the reset

    TEST_ASSERT_TRUE(usb_midi_batch_add(&b, p));   // and it is reusable
    TEST_ASSERT_EQUAL_INT(4, b.len);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_four_outputs_all_fit_in_one_transfer);
    RUN_TEST(test_full_transfer_refuses_and_counts);
    RUN_TEST(test_reset_clears_bytes_but_keeps_drop_count);
    return UNITY_END();
}
