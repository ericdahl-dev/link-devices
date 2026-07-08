// Host test for the shared measurement poll (ARC-014). Drives link_measure_pump
// against a fake ops + a link_protocol peer injected via a gossip packet, so the poll
// sequence (trigger -> send ping) is testable without sockets.
#include "unity.h"
#include "link_measure_pump.h"
#include "link_measurement.h"
#include "link_measurement_session.h"
#include "link_protocol.h"
#include <string.h>

/* ---- fake ops ---------------------------------------------------------- */
static uint32_t s_sent_ip;
static uint16_t s_sent_port;
static int      s_sent_len;
static int      s_send_count;
static int64_t  s_clock;

static void fake_send_to(uint32_t ip, uint16_t port, const uint8_t* buf, int len) {
    (void)buf;
    s_sent_ip = ip; s_sent_port = port; s_sent_len = len; s_send_count++;
}
static int  fake_recv_one(uint8_t* buf, int cap) { (void)buf; (void)cap; return 0; }  // no pongs queued
static int64_t fake_now(void) { return s_clock; }

static const LinkMeasureOps OPS = { fake_send_to, fake_recv_one, fake_now };

static LinkSession s_session;

void setUp(void) {
    s_sent_ip = 0; s_sent_port = 0; s_sent_len = 0; s_send_count = 0; s_clock = 1000;
    link_proto_reset();
    link_session_reset(&s_session);
    link_measurement_reset();
}
void tearDown(void) {}

// Minimal Alive packet with a Timeline + mep4 endpoint (see test_link_protocol).
static void inject_peer(uint32_t ip, uint16_t port) {
    uint8_t p[512]; int i = 0;
    const uint8_t magic[8] = {'_','a','s','d','p','_','v', 1};
    memcpy(p + i, magic, 8); i += 8;
    p[i++] = 1; p[i++] = 3; p[i++] = 0; p[i++] = 0;         // Alive, ttl, groupId
    for (int k = 0; k < 8; k++) p[i++] = (uint8_t)(0xA0 + k); // NodeId
    p[i++]=0x74;p[i++]=0x6d;p[i++]=0x6c;p[i++]=0x6e;         // 'tmln'
    p[i++]=0;p[i++]=0;p[i++]=0;p[i++]=24;                    // size 24
    for (int s = 56; s >= 0; s -= 8) p[i++] = (500000 >> s) & 0xff;  // us_per_beat
    for (int b = 0; b < 16; b++) p[i++] = 0;                 // beat_origin + time_origin = 0
    p[i++]=0x6d;p[i++]=0x65;p[i++]=0x70;p[i++]=0x34;         // 'mep4'
    p[i++]=0;p[i++]=0;p[i++]=0;p[i++]=6;                     // size 6
    p[i++]=(ip>>24)&0xff;p[i++]=(ip>>16)&0xff;p[i++]=(ip>>8)&0xff;p[i++]=ip&0xff;
    p[i++]=(port>>8)&0xff;p[i++]=port&0xff;
    link_proto_parse(p, i);
}

void test_no_peer_sends_nothing(void) {
    link_measure_pump(&s_session, &OPS);
    TEST_ASSERT_EQUAL_INT(0, s_send_count);
    TEST_ASSERT_FALSE(link_measurement_active());
}

void test_peer_triggers_a_ping_to_its_endpoint(void) {
    inject_peer(0xC0A80105, 20808);                 // 192.168.1.5:20808
    link_measure_pump(&s_session, &OPS);
    // trigger -> START_ATTEMPT + first SEND_PING through the pump's dispatch.
    TEST_ASSERT_EQUAL_INT(1, s_send_count);
    TEST_ASSERT_EQUAL_UINT32(0xC0A80105, s_sent_ip);
    TEST_ASSERT_EQUAL_UINT16(20808, s_sent_port);
    TEST_ASSERT_GREATER_THAN_INT(0, s_sent_len);    // a real ping datagram
    TEST_ASSERT_TRUE(link_measurement_active());     // attempt is now in flight
}

void test_same_peer_not_due_no_second_attempt(void) {
    inject_peer(0xC0A80105, 20808);
    link_measure_pump(&s_session, &OPS);            // first attempt (1 ping)
    s_clock += 1000;                                 // well before the 2s re-measure
    link_measure_pump(&s_session, &OPS);            // active + not due -> no new attempt
    TEST_ASSERT_EQUAL_INT(1, s_send_count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_no_peer_sends_nothing);
    RUN_TEST(test_peer_triggers_a_ping_to_its_endpoint);
    RUN_TEST(test_same_peer_not_due_no_second_attempt);
    return UNITY_END();
}
