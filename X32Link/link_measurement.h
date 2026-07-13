// Ableton Link measurement protocol — pure logic (LNK-018).
//
// Pinger-only client: ping a peer's `mep4` unicast endpoint (discovered by
// LNK-017's link_proto_peer_endpoint()), exchange a few ping/pong round
// trips, and derive this device's clock offset relative to the session's
// shared virtual clock (GHostTime). We never implement the PingResponder
// (reply) side — this firmware never broadcasts Alive/Response packets, so
// no peer will ever ping us back.
//
// Wire format (big-endian), distinct from link_protocol.c's discovery
// header:
//   magic       = "_link_v\x01"  (8 bytes)
//   messageType = uint8          kPing=1, kPong=2
//   ... TLV payload: key(4 BE) + size(4 BE) + value(size), same envelope
//       convention as link_protocol.c's discovery TLVs ...
//
// No WiFi/Arduino dependency here — testable on host. See
// link_measurement_io.cpp for the thin WiFiUDP unicast glue that drives
// this module on real hardware.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Cap of accumulated offset samples before we start evicting the oldest
// (ring buffer). Ticket calls 32 "enough for our purposes" — official Link
// SDK accumulates up to 100 but that's conservative, not a protocol
// minimum.
#define LINK_MEASUREMENT_MAX_SAMPLES 32

// Sample count at which an in-flight attempt is considered "good enough" to
// commit a GhostXForm from (io.cpp decides when to stop pinging once it
// reaches this many accumulated samples). Not specified numerically by the
// ticket ("see Acceptance Criteria for the exact target" — AC doesn't
// actually give one); picked as a judgment call: by round 4 a pinger with
// PrevGHostTime echoing has produced ~7-8 samples (2 per round after round
// 1), which is enough for a stable median while still converging in well
// under the 250ms failure window at LAN RTT speed.
#define LINK_MEASUREMENT_READY_SAMPLES 8

// Worst-case encoded length of a Ping message (magic + type + HostTime TLV
// + PrevGHostTime TLV).
#define LINK_MEASUREMENT_PING_MAX_LEN 41

// LNK-026 (bug 2): reject pong samples whose round trip (h_recv - echoed
// host_time) is negative or exceeds this bound. A stale pong left in the RX
// buffer across the idle gap between attempts echoes a host_time ~one
// remeasure interval (2 s) old; committing it underestimates the GhostXForm by
// ~rtt/2 (~1 s ~= 2 beats), skewing phase. The watchdog abandons an attempt
// after 5*50ms = 250ms of silence, so any pong older than that is by
// definition not part of a live exchange — 250ms is the principled cutoff.
#define LINK_MEASUREMENT_MAX_RTT_US 250000

/* P4-038. The offset estimate is `ghost - (h_recv + h_sent)/2` -- the MIDPOINT, which
 * assumes the delay is symmetric. It is not, once HTTP traffic shares the radio: each
 * sample then carries roughly rtt/2 of error, and the 250 ms gate above (chosen in LNK-026
 * to reject STALE pongs, not to bound ACCURACY) happily admits ~115 ms of it.
 *
 * Measured on the bench with the analyzer + the xf gauge: under web load a commit threw the
 * beat origin by 500 ms, the bar strobe wobbled 185 ms (idle: 4.9 ms), and the device
 * emitted 20% MORE clock pulses than real time allows -- because the GhostXForm is nothing
 * but an origin, and committing one STEPS it.
 *
 * Two rules, and they are complementary:
 *
 *   SLACK -- only the low-RTT samples of an attempt may vote. A sample delayed far beyond
 *   the best one is queued, therefore asymmetric, therefore biased. Median over the
 *   survivors keeps the robustness against a single liar while removing the systematic
 *   skew. (When every sample has the same RTT this changes nothing, which is why the
 *   pre-existing median tests still pass untouched.)
 *
 *   SLEW -- bound how far ONE commit may move an ESTABLISHED origin. Arriving a little late
 *   is survivable; the downbeat teleporting mid-bar is not. */
#define LINK_MEASUREMENT_RTT_SLACK_US 15000   /* a sample may exceed the attempt's best RTT
                                               * by this much and still vote */
#define LINK_MEASUREMENT_MAX_SLEW_US  20000   /* max origin move per commit (20 ms) */

/* ...but a genuine session re-origin is not noise: link_phase.c documents real ones jumping
 * by many SECONDS (~510 s seen on hardware). Slewing to that 20 ms at a time would take
 * hours, so a move this big is adopted whole. The clamp exists to reject measurement noise,
 * never to fight the session. */
#define LINK_MEASUREMENT_REORIGIN_US  1000000 /* >= 1 s: a real re-origin, adopt immediately */

typedef struct {
    int64_t intercept_us;  // signed microsecond offset, host -> ghost
    bool    valid;
} LinkGhostXForm;

/* P4-038 phase health. The GhostXForm is nothing but an ORIGIN, and attempt_end() STEPS
 * it -- no slew, no clamp. So a commit that lands badly moves the beat origin, and the bar
 * line moves with it. On the bench the bar strobe wobbled 185 ms under web load while the
 * clock task was provably healthy (gap 1.5 ms, 0 overruns, 0 drops, 0 re-primes) and the
 * device emitted 137 pulses MORE than real time allows. A stall cannot manufacture pulses;
 * a moving origin can.
 *
 * Why the RTT matters: add_pong_samples() estimates the offset as
 * `ghost - (h_recv + h_sent)/2` -- the midpoint, which ASSUMES a symmetric delay. Queue the
 * network in one direction (HTTP traffic sharing the radio) and the estimate is biased by
 * roughly half the asymmetry. The LNK-026 gate admits any RTT up to 250 ms, so it will
 * happily accept a sample carrying ~125 ms of error.
 *
 *   max_step_us >> a few ms  -> commits are THROWING the origin. That is the bar wobble.
 *   rtt_max_us  near the gate -> the samples feeding those commits are load-inflated. */
typedef struct {
    uint32_t commits;      // GhostXForm commits (lifetime)
    uint32_t last_step_us; // |origin move| at the most recent commit
    uint32_t max_step_us;  // worst |origin move| ever committed
    uint32_t rtt_min_us;   // best / worst RTT among ACCEPTED samples
    uint32_t rtt_max_us;
} LinkPhaseHealth;

// Fills `out` with the lifetime phase-health counters. Never resets.
void link_measurement_phase_health(LinkPhaseHealth* out);

// slope is hardcoded 1.0 in the real protocol too — Link does not model
// clock drift in this path.
int64_t link_ghost_xform_host_to_ghost(LinkGhostXForm xform, int64_t host_us);

/* ---------------------------------------------------------------------- */
/* Wire format: TLV build/parse for ping/pong messages                    */
/* ---------------------------------------------------------------------- */

// Build a Ping message (what we send) into buf.
//   Round 1:    HostTime{host_time_us} only           (has_prev_ghost=false)
//   Round N>1:  HostTime{host_time_us} + PrevGHostTime{prev_ghost_us}
// Returns the number of bytes written, or 0 if buf_cap is too small.
int link_measurement_build_ping(uint8_t* buf, int buf_cap,
                                 int64_t host_time_us,
                                 bool has_prev_ghost, int64_t prev_ghost_us);

typedef struct {
    bool    has_host_time;
    int64_t host_time_us;
    bool    has_prev_ghost_time;
    int64_t prev_ghost_time_us;
} LinkPingFields;

// Parse a Ping message body back out. Mainly exists for host-test
// round-tripping of link_measurement_build_ping() — production code (the
// pinger) never needs to parse its own pings.
bool link_measurement_parse_ping(const uint8_t* buf, int len, LinkPingFields* out);

typedef struct {
    bool    has_ghost_time;
    int64_t ghost_time_us;
    bool    has_host_time;       // our own echoed HostTime from the ping this pong replies to
    int64_t host_time_us;
    bool    has_prev_ghost_time; // our own echoed PrevGHostTime (round 2+)
    int64_t prev_ghost_time_us;
} LinkPongFields;

// Parse a Pong message (what we receive). TLV order is not guaranteed;
// looked up by key. SessionMembership ('sess') is present but ignored — we
// don't track session identity changes here. Returns false if the magic or
// message type don't match (not a Pong message at all); a structurally
// valid Pong with missing fields still returns true with the corresponding
// has_* flags left false.
bool link_measurement_parse_pong(const uint8_t* buf, int len, LinkPongFields* out);

/* ---------------------------------------------------------------------- */
/* Sample buffer / median / offset math                                   */
/* ---------------------------------------------------------------------- */

void link_measurement_samples_reset(void);
int  link_measurement_samples_count(void);

// Given one received pong and our local clock reading at the moment it
// arrived (h_recv), compute sample_a (always, if HostTime+GHostTime
// present) and sample_b (only if PrevGHostTime also present) per the
// closed-form offset formulas, and push whichever apply into the capped
// ring buffer (oldest evicted on overflow). Returns the number of samples
// pushed (0, 1, or 2).
int link_measurement_add_pong_samples(int64_t h_recv, const LinkPongFields* pong);

// Median of the currently accumulated samples. Returns false if the buffer
// is empty.
bool link_measurement_median(double* out_median);

/* ---------------------------------------------------------------------- */
/* Attempt lifecycle — state owned here, driven by named events from the  */
/* Arduino glue (link_measurement_io.cpp), which is the only piece that   */
/* knows about real sockets/timing.                                       */
/* ---------------------------------------------------------------------- */

// Resets all module state: sample buffer, committed GhostXForm, active flag.
void link_measurement_reset(void);

// Starts a fresh measurement attempt: clears the sample buffer, marks
// active=true. Does not touch any previously committed GhostXForm.
void link_measurement_attempt_begin(void);

// Ends the in-flight attempt.
//   success=true:  derive a GhostXForm from the current sample buffer's
//                  median and commit it (valid=true) — unless the buffer
//                  is empty, in which case this is a no-op on the xform.
//   success=false: leave the existing committed GhostXForm (if any)
//                  untouched rather than overwriting it with garbage, per
//                  the ticket's failure-handling rule.
// Either way, active becomes false.
void link_measurement_attempt_end(bool success);

LinkGhostXForm link_measurement_current_xform(void);

// The one honest question for phase validity: does a committed, trustworthy
// GhostXForm exist? Gate phase output on THIS, never on active() (ARC-002).
// Gating on active() was the "flashing dot" bug: active is true only while an
// attempt is in flight and drops the instant it commits, so phase flickered
// valid-then-invalid at exactly the moment the estimate became good.
bool link_measurement_have_phase_estimate(void);

// Lifecycle only — true while a measurement attempt is in flight. For the
// measurement pump's attempt orchestration; NOT a phase-validity signal.
// Callers deciding whether phase can be trusted must use
// link_measurement_have_phase_estimate() instead.
bool link_measurement_active(void);

#ifdef __cplusplus
}
#endif
