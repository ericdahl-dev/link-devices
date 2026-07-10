// Host tests for the pure WiFi connection lifecycle (ARC-013). Subsumes the old
// test_wifi_fallback boundary cases and adds the state-machine transitions.
#include "unity.h"
#include "wifi_conn_policy.h"

static WifiConnPolicy p;
void setUp(void)    { wifi_conn_policy_reset(&p, 0); }
void tearDown(void) {}

/* ---- cold-start budget (was wifi_fallback) ----------------------------- */

void test_disconnect_well_before_budget_retries(void) {
    TEST_ASSERT_EQUAL_INT(WCA_CONNECT, wifi_conn_policy_step(&p, WCE_DISCONNECTED, 1000000));   // 1s
    TEST_ASSERT_EQUAL_INT(WCA_CONNECT, wifi_conn_policy_step(&p, WCE_DISCONNECTED, 44000000));  // 44s
    TEST_ASSERT_EQUAL_INT(WCS_CONNECTING, p.state);
}

void test_disconnect_at_budget_gives_up_to_ap(void) {
    // Boundary is inclusive (>=) at WIFI_CONN_TIMEOUT_US, now 45s.
    TEST_ASSERT_EQUAL_INT(WCA_GIVE_UP_TO_AP, wifi_conn_policy_step(&p, WCE_DISCONNECTED, 45000000));
    TEST_ASSERT_EQUAL_INT(WCS_AP, p.state);
}

void test_budget_is_relative_to_connect_start(void) {
    wifi_conn_policy_reset(&p, 100000000);   // cold start at 100s since boot
    TEST_ASSERT_EQUAL_INT(WCA_CONNECT,       wifi_conn_policy_step(&p, WCE_DISCONNECTED, 100000000 + 44000000));
    TEST_ASSERT_EQUAL_INT(WCA_GIVE_UP_TO_AP, wifi_conn_policy_step(&p, WCE_DISCONNECTED, 100000000 + 45000000));
}

/* ---- got-IP: proven creds → retry forever, listener once --------------- */

void test_got_ip_spawns_listener_once_then_none(void) {
    TEST_ASSERT_EQUAL_INT(WCA_SPAWN_LISTENER, wifi_conn_policy_step(&p, WCE_GOT_IP, 5000000));
    TEST_ASSERT_EQUAL_INT(WCS_GOT_IP, p.state);
    TEST_ASSERT_EQUAL_INT(WCA_NONE,   wifi_conn_policy_step(&p, WCE_GOT_IP, 6000000));  // reconnect/renew
}

void test_disconnect_after_ip_retries_forever_never_ap(void) {
    wifi_conn_policy_step(&p, WCE_GOT_IP, 5000000);
    // A drop an hour later (well past the budget) must NOT fall to AP — creds proven.
    TEST_ASSERT_EQUAL_INT(WCA_CONNECT, wifi_conn_policy_step(&p, WCE_DISCONNECTED, 3600000000LL));
    TEST_ASSERT_EQUAL_INT(WCS_CONNECTING, p.state);
    // ...and it re-associates and keeps going.
    TEST_ASSERT_EQUAL_INT(WCA_NONE, wifi_conn_policy_step(&p, WCE_GOT_IP, 3601000000LL));  // listener already up
    TEST_ASSERT_EQUAL_INT(WCS_GOT_IP, p.state);
}

/* ---- AP is terminal ---------------------------------------------------- */

void test_ap_is_terminal_ignores_events(void) {
    wifi_conn_policy_step(&p, WCE_DISCONNECTED, 45000000);   // → AP
    TEST_ASSERT_EQUAL_INT(WCA_NONE, wifi_conn_policy_step(&p, WCE_DISCONNECTED, 46000000));  // no double-init
    TEST_ASSERT_EQUAL_INT(WCA_NONE, wifi_conn_policy_step(&p, WCE_GOT_IP, 47000000));
    TEST_ASSERT_EQUAL_INT(WCS_AP, p.state);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_disconnect_well_before_budget_retries);
    RUN_TEST(test_disconnect_at_budget_gives_up_to_ap);
    RUN_TEST(test_budget_is_relative_to_connect_start);
    RUN_TEST(test_got_ip_spawns_listener_once_then_none);
    RUN_TEST(test_disconnect_after_ip_retries_forever_never_ap);
    RUN_TEST(test_ap_is_terminal_ignores_events);
    return UNITY_END();
}
