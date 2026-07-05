#pragma once
// P4Hub glue: WiFi station (via the C6 hosted stack) + Ableton Link listener.
// Owns the network side; the gossip parsing is the pure, host-tested
// link_protocol.c. Exposes the current session timeline to the clock-out task.
#include <stdbool.h>
#include "link_protocol.h"   // LinkTimeline
#ifdef __cplusplus
extern "C" {
#endif

// Bring up WiFi station and, once connected, join the Link multicast group and
// start feeding datagrams to the parser. Non-blocking beyond WiFi association.
void wifi_link_start(void);

// Current Link session timeline; false if no valid session yet. Wraps the pure
// link_proto_timeline().
bool wifi_link_timeline(LinkTimeline* out);

// Number of Link peers currently seen.
int wifi_link_peers(void);

#ifdef __cplusplus
}
#endif
