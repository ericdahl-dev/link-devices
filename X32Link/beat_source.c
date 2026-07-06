#include "beat_source.h"

void beat_source_reset(BeatSource* s) {
    beat_clock_reset(&s->bc);
    s->was_locked = false;
    s->running    = false;
}

BeatSourceOut beat_source_step(BeatSource* s, bool have_session,
                               LinkGhostXForm xform, LinkTimeline tl, int64_t now_us) {
    BeatSourceOut o = { .active = false, .beats = 0.0, .locked = false, .reprime = false };

    if (!have_session) {
        // Session gone: re-prime once on the falling edge (the caller resets its
        // grids + transport there) and restart the free accumulator so the next
        // acquire is clean. Stay quiet on later idle steps.
        if (s->running) {
            beat_clock_reset(&s->bc);
            s->was_locked = false;
            s->running    = false;
            o.reprime     = true;
        }
        return o;
    }

    // Basis: a committed GhostXForm reads the true session phase; otherwise the
    // free-running local accumulator. A change of basis re-primes the grids
    // (mirrors the old `locked != phase_locked` edge; false in idle/free too, so
    // a first acquire straight into lock re-primes but into free does not).
    bool locked  = xform.valid;
    bool reprime = (locked != s->was_locked);

    double beats;
    if (locked) {
        int64_t ghost_now = link_ghost_xform_host_to_ghost(xform, now_us);
        beats = link_phase_beats_now(tl, ghost_now);
    } else {
        if (reprime) beat_clock_reset(&s->bc);   // entering free from lock: restart at 0
        beats = beat_clock_advance(&s->bc, now_us, tl.micros_per_beat);
    }

    s->was_locked = locked;
    s->running    = true;

    o.active  = true;
    o.beats   = beats;
    o.locked  = locked;
    o.reprime = reprime;
    return o;
}
