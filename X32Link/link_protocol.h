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

// Link StartStopState (transport). link_proto_playing() is the session's
// isPlaying flag; link_proto_start_stop_seen() is false until a StartStopState
// has actually been parsed (and resets when the session empties), so a caller
// can prime on the first real observation rather than on the default.
bool   link_proto_playing(void);
bool   link_proto_start_stop_seen(void);

#ifdef __cplusplus
}
#endif
