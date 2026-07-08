// Pure WiFi connection lifecycle — see wifi_conn_policy.h (ARC-013).
#include "wifi_conn_policy.h"

void wifi_conn_policy_reset(WifiConnPolicy* p, int64_t now_us) {
    p->state             = WCS_CONNECTING;
    p->got_ip_once       = false;
    p->listener_started  = false;
    p->connect_start_us  = now_us;
}

WifiConnAction wifi_conn_policy_step(WifiConnPolicy* p, WifiConnEvent e, int64_t now_us) {
    if (p->state == WCS_AP) return WCA_NONE;   // terminal until reboot

    switch (e) {
        case WCE_GOT_IP:
            p->state = WCS_GOT_IP;
            p->got_ip_once = true;
            if (!p->listener_started) {         // bring up the listener exactly once
                p->listener_started = true;
                return WCA_SPAWN_LISTENER;
            }
            return WCA_NONE;

        case WCE_DISCONNECTED:
            // The budget only governs INITIAL acquisition. Once we've ever had an IP
            // the creds are known-good, so retry forever instead of falling to AP on a
            // transient mid-session drop.
            if (!p->got_ip_once &&
                (now_us - p->connect_start_us) >= WIFI_CONN_TIMEOUT_US) {
                p->state = WCS_AP;
                return WCA_GIVE_UP_TO_AP;
            }
            p->state = WCS_CONNECTING;
            return WCA_CONNECT;
    }
    return WCA_NONE;
}
