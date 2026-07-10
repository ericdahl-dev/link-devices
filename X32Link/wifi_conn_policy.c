// Pure WiFi connection lifecycle — see wifi_conn_policy.h (ARC-013, ESP-013).
#include "wifi_conn_policy.h"

void wifi_conn_policy_reset(WifiConnPolicy* p, int64_t now_us, int nslots) {
    if (nslots < 1)                   nslots = 1;
    if (nslots > WIFI_CONN_MAX_SLOTS) nslots = WIFI_CONN_MAX_SLOTS;

    p->state             = WCS_CONNECTING;
    p->got_ip_once       = false;
    p->listener_started  = false;
    p->slot              = 0;
    p->nslots            = nslots;
    p->slot_start_us     = now_us;
}

// Each saved network gets an equal share of the one total budget, so adding
// networks costs the user nothing in time-to-config-AP.
static int64_t slot_budget_us(const WifiConnPolicy* p) {
    return WIFI_CONN_TIMEOUT_US / p->nslots;
}

WifiConnAction wifi_conn_policy_step(WifiConnPolicy* p, WifiConnEvent e, int64_t now_us) {
    if (p->state == WCS_AP) return WCA_NONE;   // terminal until reboot

    switch (e) {
        case WCE_GOT_IP:
            p->state = WCS_GOT_IP;
            p->got_ip_once = true;             // also freezes the slot walk
            if (!p->listener_started) {         // bring up the listener exactly once
                p->listener_started = true;
                return WCA_SPAWN_LISTENER;
            }
            return WCA_NONE;

        case WCE_DISCONNECTED:
            // The budget only governs INITIAL acquisition. Once we've ever had an IP
            // the creds are known-good, so retry this slot forever instead of walking
            // on, or falling to AP, on a transient mid-session drop.
            if (!p->got_ip_once && (now_us - p->slot_start_us) >= slot_budget_us(p)) {
                if (p->slot + 1 < p->nslots) {  // another saved network to try
                    p->slot++;
                    p->slot_start_us = now_us;  // fresh budget for the new slot
                    p->state = WCS_CONNECTING;
                    return WCA_TRY_SLOT;
                }
                p->state = WCS_AP;              // out of networks
                return WCA_GIVE_UP_TO_AP;
            }
            p->state = WCS_CONNECTING;
            return WCA_CONNECT;
    }
    return WCA_NONE;
}
