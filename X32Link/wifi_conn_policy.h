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

// STA association budget: give the configured networks this long, IN TOTAL, to
// connect on a cold start before falling back to the config AP. Hidden SSIDs +
// Wi-Fi 6 routers can take longer than a visible beacon scan.
//
// ESP-013: with N saved networks each slot gets TIMEOUT/N. Splitting the existing
// budget rather than adding a per-slot one keeps a 3-network device reaching the
// config AP in the same 45s a 1-network device always did — otherwise the user
// waits over two minutes to see the setup portal.
#define WIFI_CONN_TIMEOUT_US (45LL * 1000000LL)

// Cap on saved networks. Bounds the per-slot budget (45s/4 ≈ 11s) so the walk
// never degrades into a scan too short to associate.
#define WIFI_CONN_MAX_SLOTS 4

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
    WCA_CONNECT,          // (re)issue an STA connect on the CURRENT slot (glue adds backoff)
    WCA_TRY_SLOT,         // ESP-013: reconfigure STA for slot `p->slot`, then connect
    WCA_GIVE_UP_TO_AP,    // stop STA, bring up the config AP (terminal until reboot)
    WCA_SPAWN_LISTENER,   // start the Link listener — emitted exactly once, on first IP
} WifiConnAction;

typedef struct {
    WifiConnState state;
    bool     got_ip_once;       // ever had an IP → retry forever, never fall to AP
    bool     listener_started;  // spawn the Link listener exactly once
    int      slot;              // ESP-013: which saved network we are trying (0..nslots-1)
    int      nslots;            // how many usable networks the glue handed us (>= 1)
    int64_t  slot_start_us;     // when the CURRENT slot's attempt began (budget base)
} WifiConnPolicy;

// Start a fresh cold-start attempt at slot 0: CONNECTING, budget clock from now_us.
//
// `nslots` is the count of USABLE saved networks. The policy is deliberately blind
// to SSIDs: the glue compacts empty slots away and passes the count, so "skip an
// empty slot" never becomes a state in here. Values < 1 are clamped to 1 (a device
// with zero usable networks goes to AP by the normal budget path, not by dividing
// by zero); values above WIFI_CONN_MAX_SLOTS are clamped to the cap.
void wifi_conn_policy_reset(WifiConnPolicy* p, int64_t now_us, int nslots);

// Advance the machine on one event; returns the action for the glue to execute.
// - GOT_IP: → GOT_IP; first time also emits WCA_SPAWN_LISTENER. Freezes the walk:
//   this slot's creds are proven, so we never advance off it again.
// - DISCONNECTED, still acquiring: retry the current slot (WCA_CONNECT) until its
//   TIMEOUT/nslots share elapses, then advance to the next slot (WCA_TRY_SLOT), or
//   give up to AP (WCA_GIVE_UP_TO_AP) if that was the last one.
// - DISCONNECTED, after an IP: WCA_CONNECT forever on the proven slot; never AP.
// - Once in AP: every event is WCA_NONE (terminal until reboot).
WifiConnAction wifi_conn_policy_step(WifiConnPolicy* p, WifiConnEvent e, int64_t now_us);

#ifdef __cplusplus
}
#endif
