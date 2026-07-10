// Host tests for the pure mDNS hostname derivation (ESP-012).
#include "unity.h"
#include "ks_hostname.h"
#include <string.h>

static char h[KS_HOSTNAME_MAX];
void setUp(void)    { memset(h, 0, sizeof(h)); }
void tearDown(void) {}

// Two units on one LAN must not collide, so the name carries the last two MAC
// bytes. Lowercase hex: DNS names are case-insensitive but mDNS responders and
// browsers echo them back verbatim, and a mixed-case name reads like a bug.
void test_hostname_from_last_two_mac_bytes(void) {
    const uint8_t mac[6] = {0x3c, 0x84, 0x27, 0x11, 0xa1, 0xb2};
    ks_hostname(mac, h, sizeof(h));
    TEST_ASSERT_EQUAL_STRING("kitchensync-a1b2", h);
}

void test_hostname_pads_low_bytes(void) {
    const uint8_t mac[6] = {0, 0, 0, 0, 0x00, 0x0f};
    ks_hostname(mac, h, sizeof(h));
    TEST_ASSERT_EQUAL_STRING("kitchensync-000f", h);   // not "kitchensync-f"
}

void test_hostname_distinct_per_unit(void) {
    char a[KS_HOSTNAME_MAX], b[KS_HOSTNAME_MAX];
    const uint8_t m1[6] = {0x3c, 0x84, 0x27, 0x11, 0xde, 0xad};
    const uint8_t m2[6] = {0x3c, 0x84, 0x27, 0x11, 0xbe, 0xef};   // same OUI, same prefix
    ks_hostname(m1, a, sizeof(a));
    ks_hostname(m2, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("kitchensync-dead", a);
    TEST_ASSERT_EQUAL_STRING("kitchensync-beef", b);
    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
}

// The buffer is sized by KS_HOSTNAME_MAX; a caller passing something smaller must
// get a truncated-but-terminated string, never a half-written name it then
// advertises. Truncation is visible (short name) rather than silent corruption.
void test_hostname_truncates_safely(void) {
    const uint8_t mac[6] = {0, 0, 0, 0, 0xa1, 0xb2};
    char small[8];
    ks_hostname(mac, small, sizeof(small));
    TEST_ASSERT_EQUAL_INT(7, (int)strlen(small));   // fits exactly 7 chars + NUL
    TEST_ASSERT_EQUAL_STRING("kitchen", small);
}

// KS_HOSTNAME_MAX must actually hold the longest name this can produce, or every
// caller's stack buffer is one byte short.
void test_hostname_max_is_big_enough(void) {
    const uint8_t mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    ks_hostname(mac, h, sizeof(h));
    TEST_ASSERT_EQUAL_STRING("kitchensync-ffff", h);
    TEST_ASSERT_TRUE(strlen(h) < KS_HOSTNAME_MAX);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hostname_from_last_two_mac_bytes);
    RUN_TEST(test_hostname_pads_low_bytes);
    RUN_TEST(test_hostname_distinct_per_unit);
    RUN_TEST(test_hostname_truncates_safely);
    RUN_TEST(test_hostname_max_is_big_enough);
    return UNITY_END();
}
