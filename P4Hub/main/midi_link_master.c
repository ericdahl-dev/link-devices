#include "midi_link_master.h"
#include <math.h>

LinkTimeline midi_link_master_timeline(double bpm, uint32_t pulse_count,
                                       int64_t now_us,
                                       const LinkTimeline* observed,
                                       bool have_observed) {
    LinkTimeline tl;
    tl.micros_per_beat = (bpm > 0.0) ? (int64_t)llround(60.0e6 / bpm) : 0;
    tl.time_origin_us  = now_us;

    if (have_observed && observed && observed->micros_per_beat > 0) {
        // Take over an existing session: keep the beat continuous at `now` and
        // outrank the current timeline. Mirrors Link setTempo's
        // newBeatOrigin = max(session.toBeats(now), session.beatOrigin + 1).
        double obeat0   = (double)observed->beat_origin_micro / 1.0e6;
        double cur_beat = obeat0 + (double)(now_us - observed->time_origin_us)
                                       / (double)observed->micros_per_beat;
        double bump     = obeat0 + 1.0;                 // strict-increase floor
        double origin   = (cur_beat > bump) ? cur_beat : bump;
        tl.beat_origin_micro = (int64_t)llround(origin * 1.0e6);
    } else {
        // No session to stay continuous with: anchor beatOrigin at our own MIDI
        // beat count (beat = pulses/24) at `now`.
        double beat = (double)pulse_count / 24.0;
        tl.beat_origin_micro = (int64_t)llround(beat * 1.0e6);
    }
    return tl;
}
