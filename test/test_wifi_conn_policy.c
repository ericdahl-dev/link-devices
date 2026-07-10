// Host tests for the pure WiFi connection lifecycle (ARC-013). Subsumes the old
// test_wifi_fallback boundary cases and adds the state-machine transitions.
#include "unity.h"
#include "wifi_conn_policy.h"

static WifiConnPolicy p;
void setUp(void)    { wifi_conn_policy_reset(&p, 0, 1); }   // one slot = pre-ESP-013 behaviour
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
    wifi_conn_policy_reset(&p, 100000000, 1);   // cold start at 100s since boot
    TEST_ASSERT_EQUAL_INT(WCA_CONNECT,       wifi_conn_policy_step(&p, WCE_DISCONNECTED, 100000000 + 44000000));
    TEST_ASSERT_EQUAL_INT(WCA_GIVE_UP_TO_AP, wifi_conn_policy_step(&p, WCE_DISCONNECTED, 100000000 + 45000000));
}

/* ---- ESP-013: multi-slot walk ------------------------------------------ */

// 3 slots share the same total budget: 45s/3 = 15s each. A single-slot device
// still gets the whole 45s (the tests above), so no board regresses.
void test_three_slots_walk_then_ap(void) {
    wifi_conn_policy_reset(&p, 0, 3);
    TEST_ASSERT_EQUAL_INT(0, p.slot);

    // Slot 0 budget expires at 15s → advance, don't give up.
    TEST_ASSERT_EQUAL_INT(WCA_CONNECT,  wifi_conn_policy_step(&p, WCE_DISCONNECTED, 14000000));
    TEST_ASSERT_EQUAL_INT(0, p.slot);
    TEST_ASSERT_EQUAL_INT(WCA_TRY_SLOT, wifi_conn_policy_step(&p, WCE_DISCONNECTED, 15000000));
    TEST_ASSERT_EQUAL_INT(1, p.slot);

    // Slot 1's budget starts fresh at 15s, so it expires at 30s — not 15s later
    // than the *first* connect.
    TEST_ASSERT_EQUAL_INT(WCA_CONNECT,  wifi_conn_policy_step(&p, WCE_DISCONNECTED, 29000000));
    TEST_ASSERT_EQUAL_INT(1, p.slot);
    TEST_ASSERT_EQUAL_INT(WCA_TRY_SLOT, wifi_conn_policy_step(&p, WCE_DISCONNECTED, 30000000));
    TEST_ASSERT_EQUAL_INT(2, p.slot);

    // Last slot exhausted → AP. Total elapsed is still 45s.
    TEST_ASSERT_EQUAL_INT(WCA_CONNECT,       wifi_conn_policy_step(&p, WCE_DISCONNECTED, 44000000));
    TEST_ASSERT_EQUAL_INT(WCA_GIVE_UP_TO_AP, wifi_conn_policy_step(&p, WCE_DISCONNECTED, 45000000));
    TEST_ASSERT_EQUAL_INT(WCS_AP, p.state);
}

// The point of the whole ticket: the second network answers, so we stop walking.
void test_got_ip_on_later_slot_stops_the_walk(void) {
    wifi_conn_policy_reset(&p, 0, 3);
    TEST_ASSERT_EQUAL_INT(WCA_TRY_SLOT,       wifi_conn_policy_step(&p, WCE_DISCONNECTED, 15000000));
    TEST_ASSERT_EQUAL_INT(WCA_SPAWN_LISTENER, wifi_conn_policy_step(&p, WCE_GOT_IP,       16000000));
    TEST_ASSERT_EQUAL_INT(1, p.slot);

    // A drop an hour later must retry THIS slot forever, never walk on and never
    // fall to AP — these creds are proven good.
    TEST_ASSERT_EQUAL_INT(WCA_CONNECT, wifi_conn_policy_step(&p, WCE_DISCONNECTED, 3600000000LL));
    TEST_ASSERT_EQUAL_INT(1, p.slot);
    TEST_ASSERT_EQUAL_INT(WCS_CONNECTING, p.state);
}

// Glue compacts empty slots away, so 0 usable slots must not divide by zero.
void test_degenerate_slot_counts_behave_as_one(void) {
    wifi_conn_policy_reset(&p, 0, 0);
    TEST_ASSERT_EQUAL_INT(1, p.nslots);
    TEST_ASSERT_EQUAL_INT(WCA_GIVE_UP_TO_AP, wifi_conn_policy_step(&p, WCE_DISCONNECTED, 45000000));

    wifi_conn_policy_reset(&p, 0, -7);
    TEST_ASSERT_EQUAL_INT(1, p.nslots);
}

// More slots than the budget can serve must still terminate at AP, not wedge.
void test_walk_terminates_with_many_slots(void) {
    wifi_conn_policy_reset(&p, 0, WIFI_CONN_MAX_SLOTS);
    int64_t t = 0;
    for (int i = 0; i < 100; i++) {
        t += WIFI_CONN_TIMEOUT_US;   // blow each slot's budget outright
        if (wifi_conn_policy_step(&p, WCE_DISCONNECTED, t) == WCA_GIVE_UP_TO_AP) break;
    }
    TEST_ASSERT_EQUAL_INT(WCS_AP, p.state);
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
    RUN_TEST(test_three_slots_walk_then_ap);
    RUN_TEST(test_got_ip_on_later_slot_stops_the_walk);
    RUN_TEST(test_degenerate_slot_counts_behave_as_one);
    RUN_TEST(test_walk_terminates_with_many_slots);
    RUN_TEST(test_got_ip_spawns_listener_once_then_none);
    RUN_TEST(test_disconnect_after_ip_retries_forever_never_ap);
    RUN_TEST(test_ap_is_terminal_ignores_events);
    return UNITY_END();
}
