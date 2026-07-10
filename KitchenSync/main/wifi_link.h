#pragma once
// KitchenSync glue: WiFi station (via the C6 hosted stack) + Ableton Link listener.
// Owns the network side; the gossip parsing is the pure, host-tested
// link_protocol.c. Exposes the current session timeline to the clock-out task.
#include <stdbool.h>
#include "link_protocol.h"   // LinkTimeline
#include "ks_config.h"       // WifiCred
#ifdef __cplusplus
extern "C" {
#endif

// Bring up WiFi. With one or more saved networks: try each in turn (ESP-013) and,
// once connected, join the Link multicast group and feed the parser. With none
// (n == 0): start a SoftAP ("KitchenSync-Setup") so the web UI is reachable to
// enter credentials (first-boot config mode, no Link). Non-blocking beyond
// association. `creds` is copied, so the caller's buffer need not outlive the call.
//
// Pass the COMPACTED list from ks_config_wifi_slots(): empty slots are dropped
// there, so this never attempts a blank SSID.
void wifi_link_start(const WifiCred* creds, int n);

// True when running in SoftAP config mode (no station credentials).
bool wifi_link_ap_mode(void);

// Current Link session timeline; false if no valid session yet. Wraps the pure
// link_proto_timeline().
bool wifi_link_timeline(LinkTimeline* out);

// Number of Link peers currently seen.
int wifi_link_peers(void);

#ifdef __cplusplus
}
#endif
