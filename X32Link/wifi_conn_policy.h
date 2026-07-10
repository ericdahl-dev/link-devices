#pragma once
// ARC-013: the WiFi connection lifecycle as a pure state machine, shared by both
// boards. Replaces the scattered module-static flags (P4 wifi_link.c) and the
// hand-rolled blocking loop with its inlined `> 30000` literal (S3 X32Link.ino), so
// the give-up budget + the "retry forever once we've had an IP" rule live in ONE
// host-tested place (subsumes the old wifi_fallback).
//
// The glue translates its platform events (esp_wifi events / WiFi.status() polls)
// into WifiConnEvent, calls step(), and executes the returned action. No I/O here.
// See test/test_wifi_conn_policy.c.
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// STA association budget: give the configured network this long to connect on a cold
// start before falling back to the config AP. Hidden SSIDs + Wi-Fi 6 routers can
// take longer than a visible beacon scan.
#define WIFI_CONN_TIMEOUT_US (45LL * 1000000LL)

typedef enum {
    WCS_CONNECTING = 0,   // trying to associate (initial or retry)
    WCS_GOT_IP,           // associated + DHCP at least once — creds proven good
    WCS_AP,               // gave up on STA, parked in the config AP (terminal)
} WifiConnState;

typedef enum {
    WCE_DISCONNECTED = 1, // STA disconnected / not-yet-connected poll
    WCE_GOT_IP,           // got a DHCP lease
} WifiConnEvent;

typedef enum {
    WCA_NONE = 0,
    WCA_CONNECT,          // (re)issue an STA connect (glue adds any backoff)
    WCA_GIVE_UP_TO_AP,    // stop STA, bring up the config AP (terminal until reboot)
    WCA_SPAWN_LISTENER,   // start the Link listener — emitted exactly once, on first IP
} WifiConnAction;

typedef struct {
    WifiConnState state;
    bool     got_ip_once;       // ever had an IP → retry forever, never fall to AP
    bool     listener_started;  // spawn the Link listener exactly once
    int64_t  connect_start_us;  // when the cold-start STA attempt began (budget base)
} WifiConnPolicy;

// Start a fresh cold-start attempt: CONNECTING, budget clock from now_us.
void wifi_conn_policy_reset(WifiConnPolicy* p, int64_t now_us);

// Advance the machine on one event; returns the action for the glue to execute.
// - GOT_IP: → GOT_IP; first time also emits WCA_SPAWN_LISTENER.
// - DISCONNECTED: gives up to AP only during INITIAL acquisition (never had an IP)
//   once WIFI_CONN_TIMEOUT_US has elapsed; otherwise WCA_CONNECT (retry). Once in AP,
//   every event is WCA_NONE (terminal until reboot).
WifiConnAction wifi_conn_policy_step(WifiConnPolicy* p, WifiConnEvent e, int64_t now_us);

#ifdef __cplusplus
}
#endif
