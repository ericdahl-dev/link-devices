#pragma once
// LNK-031: the Link measurement-session orchestrator, pulled out of the thin
// WiFiUDP glue (link_measurement_io.cpp) into a pure, host-tested state machine.
// This owns the *policy* — peer targeting, re-measure scheduling, the transport
// re-origin (epoch) response, the RX-flush on a fresh attempt, and the silence
// watchdog / timeout retry — all of which used to live untestable in glue and
// was where both LNK-026 bugs hid.
//
// Shape: the glue gathers facts (peer endpoint, gossiped timeline, socket
// events, clock) and calls the on_* event functions; each returns an ordered
// list of ACTIONS for the glue to execute (flush the socket, begin/commit an
// attempt, send a ping, reset the committed xform). No side effects here — every
// decision is a pure, replayable step. No Arduino headers. See
// test/test_link_measurement_session.c.
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Policy constants (moved here from the glue — they are decisions, not I/O).
#define LINK_SESSION_WATCHDOG_US    50000     // 50 ms silence watchdog
#define LINK_SESSION_MAX_TIMEOUTS   5         // 5 * 50 ms = 250 ms -> abandon attempt
#define LINK_SESSION_REMEASURE_US   2000000   // 2 s periodic re-measure
// P4-028: a backward time_origin jump must PERSIST this long before it counts as a
// genuine re-origin. A joining peer briefly gossips its own un-synced timeline
// (last-writer-wins in link_protocol.c makes it look like a re-origin here); the
// real session origin returns within a gossip cycle and clears the candidate. Tuned
// to the observed ~500 ms peer-join transient; validate/tune on hardware.
#define LINK_SESSION_EPOCH_SETTLE_US 500000   // 500 ms

typedef enum {
    LS_FLUSH_RX = 1,     // discard any buffered pongs before a fresh attempt
    LS_START_ATTEMPT,    // begin sample collection (link_measurement_attempt_begin)
    LS_SEND_PING,        // send a ping to {ip,port} (has_prev_ghost/prev_ghost_us) at send_time_us
    LS_END_OK,           // commit the attempt (link_measurement_attempt_end(true))
    LS_END_FAIL,         // abandon the attempt (link_measurement_attempt_end(false))
    LS_RESET_XFORM,      // invalidate the committed GhostXForm (link_measurement_reset)
} LinkSessionActType;

typedef struct {
    LinkSessionActType type;
    uint32_t ip;             // LS_SEND_PING target (wire-order be32)
    uint16_t port;           // LS_SEND_PING target
    bool     has_prev_ghost; // LS_SEND_PING: include a PrevGHostTime TLV?
    int64_t  prev_ghost_us;  // LS_SEND_PING PrevGHostTime value
    int64_t  send_time_us;   // LS_SEND_PING HostTime value
} LinkSessionAct;

typedef struct {
    bool     have_ref;             // do we currently target a reference peer?
    uint32_t ref_ip;
    uint16_t ref_port;
    bool     have_prev_ghost;      // carry PrevGHostTime into the next ping (round 2+)
    int64_t  prev_ghost_us;
    int64_t  last_send_us;         // last ping send time (drives the watchdog)
    int      consecutive_timeouts; // silence watchdog fires this many times -> abandon
    int64_t  next_measure_us;      // when the periodic re-measure is due
    bool     have_last_origin;     // seen a timeline yet (for epoch-reset detection)?
    int64_t  last_time_origin_us;
    bool     epoch_pending;        // P4-028: a backward jump is being debounced
    int64_t  epoch_pending_since_us; // when the pending jump was first seen
} LinkSession;

// Reset all session state (call from the glue's begin()).
void link_session_reset(LinkSession* s);

// Timeline gossip observed this poll. tl_valid=false is a no-op (no timeline
// yet). A backward time_origin jump past the threshold is DEBOUNCED (P4-028): it
// must persist LINK_SESSION_EPOCH_SETTLE_US before it counts as a genuine re-origin
// and emits LS_RESET_XFORM (forgetting the ref, zeroing next_measure). A transient
// junk timeline from a joining peer is cleared when the real origin returns within
// the window. now_us is the caller's monotonic clock. Returns action count.
int link_session_on_timeline(LinkSession* s, bool tl_valid, int64_t time_origin_us,
                             int64_t now_us, LinkSessionAct* out, int max);

// Trigger check each poll. peer_found + {ip,port} come from
// link_proto_peer_endpoint(); active mirrors link_measurement_active(). Starts a
// fresh attempt (FLUSH_RX, START_ATTEMPT, first SEND_PING) when a different peer
// appeared or the re-measure timer is due and no attempt is in flight. If no peer
// endpoint is available, forgets the reference. Returns action count.
int link_session_on_trigger(LinkSession* s, bool peer_found, uint32_t ip, uint16_t port,
                            int64_t now_us, bool active, LinkSessionAct* out, int max);

// A pong arrived; the glue has already added its samples, so samples_count is the
// post-add total. Resets the timeout counter; emits LS_END_OK once
// LINK_MEASUREMENT_READY_SAMPLES is reached, else carries this pong's GHostTime
// into the next ping and emits SEND_PING. h_recv is the local receive time.
int link_session_on_pong(LinkSession* s, int64_t h_recv, bool has_ghost, int64_t ghost_us,
                         int samples_count, LinkSessionAct* out, int max);

// Silence watchdog — call once per poll while an attempt is active. After
// WATCHDOG_US of silence it re-pings; after MAX_TIMEOUTS it emits LS_END_FAIL
// (leaving any committed xform untouched). Returns action count.
int link_session_on_watchdog(LinkSession* s, int64_t now_us, LinkSessionAct* out, int max);

#ifdef __cplusplus
}
#endif
