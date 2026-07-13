// P4-040: internal/tap tempo source + Link/internal arbiter — see master_clock.h.
#include "master_clock.h"
#include <string.h>
#include <math.h>

void master_clock_reset(MasterClock* mc) {
    memset(mc, 0, sizeof(*mc));
}

void master_clock_set_bpm(MasterClock* mc, float bpm) {
    if (bpm <= 0.0f) return;
    mc->micros_per_beat = (int64_t)llround(60.0e6 / (double)bpm);
    mc->has_tempo = true;
}

void master_clock_tap(MasterClock* mc, int64_t now_us) {
    if (mc->has_last_tap) {
        int64_t interval = now_us - mc->last_tap_us;
        if (interval > 0 && interval <= MASTER_CLOCK_TAP_TIMEOUT_US) {
            mc->micros_per_beat = interval;
            mc->has_tempo       = true;
        }
    }
    mc->last_tap_us  = now_us;
    mc->has_last_tap = true;
}

MasterArbiterOut master_clock_arbiter(MasterClock* mc, int link_peers,
                                       bool link_have_session, LinkTimeline link_tl,
                                       LinkGhostXForm link_xform) {
    bool solo = (link_peers == 0);

    // Rising edge into solo only (guarded by was_solo, so a live tap taken
    // during an already-solo run is never clobbered on a later tick): seed
    // from whatever Link is currently showing, overwriting any stale tempo
    // held from a previous solo period, so output is continuous. If Link has
    // nothing to seed from, leave whatever tempo the user last set alone.
    if (solo && !mc->was_solo && link_have_session && link_tl.micros_per_beat > 0) {
        mc->micros_per_beat = link_tl.micros_per_beat;
        mc->has_tempo       = true;
    }
    mc->was_solo = solo;

    MasterArbiterOut out;
    memset(&out, 0, sizeof(out));

    if (!solo) {
        out.have_session = link_have_session;
        out.tl           = link_tl;
        out.xform        = link_xform;
        out.on_internal  = false;
        return out;
    }

    if (mc->has_tempo) {
        out.have_session         = true;
        out.tl.micros_per_beat   = mc->micros_per_beat;
        out.xform.valid          = false;
        out.on_internal          = true;
        return out;
    }

    out.have_session = false;
    out.on_internal  = false;
    return out;
}
