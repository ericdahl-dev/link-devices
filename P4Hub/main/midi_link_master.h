#pragma once
// Pure timeline construction for P4-011 "MIDI-in is tempo master": turn a MIDI-
// derived BPM into an Ableton Link Timeline to broadcast so other peers adopt our
// tempo. No ESP-IDF deps -> host-tested in test/test_midi_link_master.c.
//
// Link adoption rule (from the Link SDK, ClientSessionTimelines/Sessions):
//   * A peer's Timeline replaces the session's only when its beatOrigin is
//     GREATER than the session's current beatOrigin. beatOrigin is a monotonic
//     priority: Link's own setTempo picks
//         newBeatOrigin = max(session.toBeats(now), session.beatOrigin + 1)
//     which both preserves the beat at `now` (continuity) and strictly outranks
//     the current timeline. We mirror that exactly.
//   * timeOrigin/beatOrigin are a reference point, not "the current beat":
//     beat(t) = beatOrigin + (t - timeOrigin)/microsPerBeat.
//
// Ghost-time caveat: true cross-peer PHASE alignment needs the session ghost
// clock (ping/pong measurement). Here `now_us` is the local monotonic clock, so
// the published TEMPO is correct and beat continuity is preserved on our own
// clock, but absolute downbeat phase across peers is best-effort. Tempo
// propagation is the P4-011 deliverable; phase-accurate master is a follow-up.
#include <stdint.h>
#include <stdbool.h>
#include "link_protocol.h"   /* LinkTimeline */
#ifdef __cplusplus
extern "C" {
#endif

// Build a Link Timeline publishing `bpm` as the session tempo.
//   bpm         : MIDI-derived tempo (from midi_clock_in_bpm); must be > 0.
//   pulse_count : total 24-PPQN pulses seen (beat = pulse_count/24) — the beat
//                 origin when there is no observed session to stay continuous with.
//   now_us      : local monotonic microseconds (esp_timer_get_time()).
//   observed    : the current session Timeline we heard on the wire, or NULL.
//   have_observed: true iff `observed` is a live session timeline to take over.
// Returns the Timeline to encode + broadcast via link_proto_build_alive().
LinkTimeline midi_link_master_timeline(double bpm, uint32_t pulse_count,
                                       int64_t now_us,
                                       const LinkTimeline* observed,
                                       bool have_observed);

#ifdef __cplusplus
}
#endif
