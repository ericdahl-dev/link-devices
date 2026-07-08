// Pure packet parsing logic for the Ableton Link gossip protocol.
// No WiFi/Arduino dependency — testable on host.
//
// Wire format (big-endian):
//   [8]  magic '_asdp_v\x01'
//   [1]  msgType  1=Alive  2=Response  3=ByeBye
//   [1]  ttl
//   [2]  groupId
//   [8]  NodeId
//   TLV loop: key(4) + size(4) + value(size)
//   Timeline key 0x746d6c6e: value[0..7] = microsPerBeat (int64 BE)
//   BPM = 60e6 / microsPerBeat

#include "link_protocol.h"
#include "session_timeline.h"   // ARC-011: settled-timeline owner + epoch debounce
#include <string.h>

static const uint8_t  MAGIC[8]     = {'_','a','s','d','p','_','v', 1};
static const uint32_t TMLN_KEY     = 0x746d6c6eu;
static const uint32_t MEP4_KEY     = 0x6d657034u;
static const uint32_t STST_KEY     = 0x73747374u;  // Link StartStopState ('stst')
static const uint32_t PEER_TTL_MS  = 15000;

static const uint8_t MSG_ALIVE    = 1;
static const uint8_t MSG_RESPONSE = 2;
static const uint8_t MSG_BYEBYE   = 3;

typedef struct {
    uint8_t  id[8];
    uint32_t expires_ms;
    uint32_t mep4_ip;
    uint16_t mep4_port;
    bool     has_mep4;
} Peer;

static double       s_bpm        = 0.0;
static Peer         s_peers[8];
static int          s_peer_count = 0;
static LinkTimeline s_timeline;                 // raw gossip (last-writer-wins)
static bool         s_timeline_seen = false;
static SessionTimeline s_stl;                   // ARC-011: settled view served to consumers
static volatile bool   s_epoch_reset_pending = false;  // latched on a confirmed re-origin
static bool         s_playing       = false;  // Link StartStopState isPlaying
static bool         s_stst_seen     = false;  // have we parsed a StartStopState yet

// millis() shim — overridden in tests via weak symbol
uint32_t __attribute__((weak)) link_proto_millis(void) { return 0; }

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|(uint32_t)p[3];
}

static int64_t be64(const uint8_t* p) {
    int64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v<<8)|p[i];
    return v;
}

static uint16_t be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0]<<8)|(uint16_t)p[1]);
}

static int find_peer(const uint8_t* id) {
    for (int i = 0; i < s_peer_count; i++)
        if (memcmp(s_peers[i].id, id, 8) == 0) return i;
    return -1;
}

static void upsert_peer(const uint8_t* id) {
    int idx = find_peer(id);
    if (idx >= 0) { s_peers[idx].expires_ms = link_proto_millis() + PEER_TTL_MS; return; }
    if (s_peer_count < 8) {
        memcpy(s_peers[s_peer_count].id, id, 8);
        s_peers[s_peer_count].expires_ms = link_proto_millis() + PEER_TTL_MS;
        s_peers[s_peer_count].mep4_ip    = 0;
        s_peers[s_peer_count].mep4_port  = 0;
        s_peers[s_peer_count].has_mep4   = false;
        s_peer_count++;
    }
}

static void remove_peer(const uint8_t* id) {
    int idx = find_peer(id);
    if (idx < 0) return;
    s_peers[idx] = s_peers[--s_peer_count];
    if (s_peer_count == 0) s_bpm = 0.0;  // no session → no tempo
    if (s_peer_count == 0) { s_playing = false; s_stst_seen = false; }
}

void link_proto_reset(void) {
    s_bpm        = 0.0;
    s_peer_count = 0;
    s_timeline_seen = false;
    s_playing    = false;
    s_stst_seen  = false;
    memset(&s_timeline, 0, sizeof(s_timeline));
    session_timeline_reset(&s_stl);
    s_epoch_reset_pending = false;
}

// Drop peers whose TTL elapsed without a refreshing Alive/Response.
// Call periodically — Link peers that vanish without a ByeBye expire here.
void link_proto_tick(void) {
    uint32_t now = link_proto_millis();
    for (int i = 0; i < s_peer_count; ) {
        if ((int32_t)(now - s_peers[i].expires_ms) >= 0)
            s_peers[i] = s_peers[--s_peer_count];   // swap-remove, recheck slot i
        else
            i++;
    }
    if (s_peer_count == 0) s_bpm = 0.0;  // no session → no tempo
    if (s_peer_count == 0) { s_playing = false; s_stst_seen = false; }
}

bool link_proto_parse(const uint8_t* buf, int len) {
    if (len < 20) return false;
    if (memcmp(buf, MAGIC, 8) != 0) return false;

    uint8_t msgType       = buf[8];
    const uint8_t* nodeId = buf + 12;

    if (msgType == MSG_BYEBYE) { remove_peer(nodeId); return true; }
    if (msgType != MSG_ALIVE && msgType != MSG_RESPONSE) return false;

    upsert_peer(nodeId);

    const uint8_t* p   = buf + 20;
    const uint8_t* end = buf + len;
    while (p + 8 <= end) {
        uint32_t key  = be32(p); p += 4;
        uint32_t size = be32(p); p += 4;
        if (p + size > end) break;
        if (key == TMLN_KEY && size >= 8) {
            int64_t us_per_beat = be64(p);
            if (us_per_beat > 0) s_bpm = 60.0e6 / (double)us_per_beat;
            s_timeline.micros_per_beat = us_per_beat;
            if (size >= 24) {
                s_timeline.beat_origin_micro = be64(p + 8);
                s_timeline.time_origin_us    = be64(p + 16);
            }
            s_timeline_seen = true;
            // ARC-011: debounce the epoch here so BOTH the beat math (settled read)
            // and the measurement glue (the pending latch) see one settled timeline.
            if (session_timeline_observe(&s_stl, true, s_timeline,
                                         (int64_t)link_proto_millis() * 1000))
                s_epoch_reset_pending = true;
        }
        if (key == MEP4_KEY && size >= 6) {
            int idx = find_peer(nodeId);
            if (idx >= 0) {
                s_peers[idx].mep4_ip   = be32(p);
                s_peers[idx].mep4_port = be16(p + 4);
                s_peers[idx].has_mep4  = true;
            }
        }
        // StartStopState: isPlaying(1) + beats(int64 BE) + timestamp(int64 BE).
        // Only isPlaying is needed to drive MIDI transport (P4-008).
        if (key == STST_KEY && size >= 1) {
            s_playing   = (p[0] != 0);
            s_stst_seen = true;
        }
        p += size;
    }
    return true;
}

double link_proto_bpm(void)   { return s_bpm; }

bool link_proto_playing(void)         { return s_playing; }
bool link_proto_start_stop_seen(void) { return s_stst_seen; }
int    link_proto_peers(void) { return s_peer_count; }

bool link_proto_timeline(LinkTimeline* out) {
    // ARC-011: serve the settled (held-last-good, epoch-debounced) timeline, not the
    // raw store — so a joining peer's un-synced gossip never reaches the beat math.
    return session_timeline_settled(&s_stl, out);
}

// ARC-011: true once (then clears) when the settled-timeline debounce confirms a
// genuine re-origin; the measurement glue polls this to reset the committed xform.
bool link_proto_epoch_reset_pending(void) {
    bool p = s_epoch_reset_pending;
    s_epoch_reset_pending = false;
    return p;
}

bool link_proto_peer_endpoint(int index, uint32_t* ip, uint16_t* port) {
    if (index < 0 || index >= s_peer_count) return false;
    if (!s_peers[index].has_mep4) return false;
    if (ip)   *ip   = s_peers[index].mep4_ip;
    if (port) *port = s_peers[index].mep4_port;
    return true;
}
