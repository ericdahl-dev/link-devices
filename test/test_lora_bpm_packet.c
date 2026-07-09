#include "unity.h"
#include "lora_bpm_packet.h"

void setUp(void)    {}
void tearDown(void) {}

void test_round_trip_typical_bpm(void) {
    lora_bpm_packet_t pkt = { LORA_MSG_BPM, 7, 12000 };  // 120.00 BPM, seq 7
    uint8_t buf[LORA_BPM_PACKET_SIZE];
    lora_bpm_packet_encode(&pkt, buf);

    lora_bpm_packet_t out;
    TEST_ASSERT_TRUE(lora_bpm_packet_decode(buf, sizeof(buf), &out));
    TEST_ASSERT_EQUAL_UINT8(LORA_MSG_BPM, out.msg_type);
    TEST_ASSERT_EQUAL_UINT8(7, out.seq);
    TEST_ASSERT_EQUAL_UINT16(12000, out.bpm_x100);
}

void test_round_trip_zero_bpm(void) {
    lora_bpm_packet_t pkt = { LORA_MSG_NO_SESSION, 0, 0 };
    uint8_t buf[LORA_BPM_PACKET_SIZE];
    lora_bpm_packet_encode(&pkt, buf);

    lora_bpm_packet_t out;
    TEST_ASSERT_TRUE(lora_bpm_packet_decode(buf, sizeof(buf), &out));
    TEST_ASSERT_EQUAL_UINT8(LORA_MSG_NO_SESSION, out.msg_type);
    TEST_ASSERT_EQUAL_UINT16(0, out.bpm_x100);
}

void test_round_trip_max_bpm_x100(void) {
    lora_bpm_packet_t pkt = { LORA_MSG_BPM, 255, 65535 };  // seq wraparound value + max u16
    uint8_t buf[LORA_BPM_PACKET_SIZE];
    lora_bpm_packet_encode(&pkt, buf);

    lora_bpm_packet_t out;
    TEST_ASSERT_TRUE(lora_bpm_packet_decode(buf, sizeof(buf), &out));
    TEST_ASSERT_EQUAL_UINT8(255, out.seq);
    TEST_ASSERT_EQUAL_UINT16(65535, out.bpm_x100);
}

void test_decode_rejects_short_buffer(void) {
    uint8_t buf[LORA_BPM_PACKET_SIZE] = {1, 2, 3, 4};
    lora_bpm_packet_t out;
    TEST_ASSERT_FALSE(lora_bpm_packet_decode(buf, LORA_BPM_PACKET_SIZE - 1, &out));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_typical_bpm);
    RUN_TEST(test_round_trip_zero_bpm);
    RUN_TEST(test_round_trip_max_bpm_x100);
    RUN_TEST(test_decode_rejects_short_buffer);
    return UNITY_END();
}
