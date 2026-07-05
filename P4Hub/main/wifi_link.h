#pragma once
// P4Hub glue: WiFi station (via the C6 hosted stack) + Ableton Link listener.
// Owns the network side; the gossip parsing is the pure, host-tested
// link_protocol.c. Exposes the current session timeline to the clock-out task.
#include <stdbool.h>
#include "link_protocol.h"   // LinkTimeline
#ifdef __cplusplus
extern "C" {
#endif

// Bring up WiFi. With a non-empty ssid: join as a station and, once connected,
// join the Link multicast group and feed the parser. With an empty ssid: start a
// SoftAP ("P4Hub-Setup") so the web UI is reachable to enter credentials
// (first-boot config mode, no Link). Non-blocking beyond association.
void wifi_link_start(const char* ssid, const char* pass);

// True when running in SoftAP config mode (no station credentials).
bool wifi_link_ap_mode(void);

// Current session timeline the clock-out task should follow; false if none.
// In MIDI-master mode (P4-011) this returns the override set by
// wifi_link_set_master_timeline(); otherwise it wraps the pure Link-parsed
// link_proto_timeline(). One accessor so clock-out/metronome are source-agnostic.
bool wifi_link_timeline(LinkTimeline* out);

// P4-011: install (tl != NULL) or clear (tl == NULL) the MIDI-master timeline
// override that wifi_link_timeline() returns. Setting it makes clock-out/
// metronome follow the MIDI-derived tempo; clearing it restores Link-follow.
void wifi_link_set_master_timeline(const LinkTimeline* tl);

// P4-011: multicast one ALIVE gossip packet carrying `tl` to the Link group so
// other peers adopt our tempo. Uses this module's own random 8-byte nodeId and,
// when a session has been observed, that session's id (link_proto_session_id) so
// the timeline is eligible for adoption. No-op until the Link socket is open.
void wifi_link_send_alive(const LinkTimeline* tl);

// Number of Link peers currently seen.
int wifi_link_peers(void);

#ifdef __cplusplus
}
#endif
