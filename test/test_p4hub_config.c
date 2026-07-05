// Host tests for the pure P4Hub runtime config (P4-007).
#include "unity.h"
#include "p4hub_config.h"
#include <string.h>

static P4HubConfig c;
void setUp(void)    { p4hub_config_defaults(&c); }
void tearDown(void) {}

void test_defaults(void) {
    TEST_ASSERT_EQUAL_INT(1, c.clock_out_enable);
    TEST_ASSERT_EQUAL_INT(0, c.midi_cable);
    TEST_ASSERT_EQUAL_STRING("", c.wifi_ssid);
    TEST_ASSERT_TRUE(p4hub_config_valid(&c));   // empty ssid is valid (AP mode)
}

void test_set_ssid_and_pass(void) {
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "wifi_ssid", "TestNet"));
    TEST_ASSERT_EQUAL_STRING("TestNet", c.wifi_ssid);
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "wifi_pass", "secret"));
    TEST_ASSERT_EQUAL_STRING("secret", c.wifi_pass);
}

// Blank password field keeps the current password (never wipes it).
void test_blank_pass_keeps_current(void) {
    p4hub_config_set(&c, "wifi_pass", "keepme");
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "wifi_pass", ""));
    TEST_ASSERT_EQUAL_STRING("keepme", c.wifi_pass);
}

void test_cable_range(void) {
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "midi_cable", "3"));
    TEST_ASSERT_EQUAL_INT(3, c.midi_cable);
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "midi_cable", "4"));  // out of range
    TEST_ASSERT_EQUAL_INT(3, c.midi_cable);                     // unchanged
}

void test_clock_out_toggle(void) {
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "clock_out", "0"));
    TEST_ASSERT_EQUAL_INT(0, c.clock_out_enable);
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "clock_out", "2"));   // only 0/1
}

void test_unknown_key(void) {
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "bogus", "x"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_set_ssid_and_pass);
    RUN_TEST(test_blank_pass_keeps_current);
    RUN_TEST(test_cable_range);
    RUN_TEST(test_clock_out_toggle);
    RUN_TEST(test_unknown_key);
    return UNITY_END();
}
