#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t micros_per_beat;
    int64_t beat_origin_micro;
    int64_t time_origin_us;
} LinkTimeline;

void   link_proto_reset(void);
void   link_proto_tick(void);
bool   link_proto_parse(const uint8_t* buf, int len);
double link_proto_bpm(void);
int    link_proto_peers(void);
bool   link_proto_timeline(LinkTimeline* out);
bool   link_proto_peer_endpoint(int index, uint32_t* ip, uint16_t* port);

// SessionId of the peer whose gossip we last parsed a Timeline from (the 'sess'
// SessionMembership TLV, an 8-byte NodeId). Copies 8 bytes into out and returns
// true once any 'sess' TLV has been seen; false before that. P4-011 uses this so
// a tempo-publishing node can claim the *observed* session (matching Ableton's
// sessionId) rather than staying in its own island session — a peer's Timeline is
// only adopted when it carries the session's sessionId.
bool   link_proto_session_id(uint8_t out[8]);

// Build an ALIVE gossip packet into buf (capacity cap), mirroring the exact wire
// format link_proto_parse consumes: magic + msgType=Alive + ttl + groupId(0) +
// node_id(8), then a 'sess' TLV (session_id, 8 bytes) and a 'tmln' TLV (24 bytes:
// micros_per_beat, beat_origin_micro, time_origin_us — all int64 BE). Returns the
// byte count written, or 0 if cap is too small or tl is NULL. Pure/host-tested:
// the P4-011 glue in wifi_link.c multicasts the result. To be *adopted* by other
// Link peers, session_id must match the session (see link_proto_session_id) and
// tl->beat_origin_micro must exceed the session's current beatOrigin (Link adopts
// the timeline with the greater beatOrigin) — see midi_link_master.c.
int    link_proto_build_alive(uint8_t* buf, int cap,
                              const uint8_t node_id[8], const uint8_t session_id[8],
                              const LinkTimeline* tl, uint8_t ttl);

// Link StartStopState (transport). link_proto_playing() is the session's
// isPlaying flag; link_proto_start_stop_seen() is false until a StartStopState
// has actually been parsed (and resets when the session empties), so a caller
// can prime on the first real observation rather than on the default.
bool   link_proto_playing(void);
bool   link_proto_start_stop_seen(void);

#ifdef __cplusplus
}
#endif
