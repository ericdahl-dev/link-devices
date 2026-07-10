// Pure clock-tick orchestration — see ks_tick.h (ARC-015).
#include "ks_tick.h"
#include <string.h>

void ks_tick_reset(KsTickState* st, uint32_t cfg_gen) {
    beat_source_reset(&st->src);
    for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) clock_ticker_reset(&st->cts[i]);
    metronome_reset(&st->mt);
    transport_reset(&st->tr);
    st->seen_gen = cfg_gen;
}

KsTickPlan ks_tick_step(KsTickState* st, const KsTickInputs* in) {
    KsTickPlan plan;
    memset(&plan, 0, sizeof(plan));
    plan.transport = TRANSPORT_NONE;

    // beat_source (ARC-007) owns the basis: phase-locked session beat once a
    // GhostXForm is committed vs the free-running local accumulator, plus the
    // re-prime signal on any basis switch or session loss.
    BeatSourceOut bs = beat_source_step(&st->src, in->have_session, in->xform, in->tl, in->t_now);
    plan.active = bs.active;
    plan.locked = bs.locked;
    plan.beats  = bs.beats;

    // Reprime fold: a basis switch/session loss (bs.reprime) OR a live config edit
    // (P4-015: g_cfg_gen bumped) shifts the beat grid — re-prime the tick + click
    // grids so the boundary realigns instead of dumping a catch-up burst. On the
    // session-loss edge also reset transport so a re-join doesn't fire a spurious Start.
    bool reprime = bs.reprime;
    if (in->cfg_gen != st->seen_gen) { reprime = true; st->seen_gen = in->cfg_gen; }
    if (reprime) {
        for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) clock_ticker_reset(&st->cts[i]);
        metronome_reset(&st->mt);
        if (!bs.active) transport_reset(&st->tr);
    }
    plan.reprime = reprime;

    if (!bs.active) return plan;

    // Clock fan-out: the one shared beat to each enabled output at its own division +
    // phase nudge + swing (P4-010), gated by the USB host + the master clock switch.
    if (in->usb_ready && in->cfg->clock_out_enable) {
        for (int o = 0; o < KS_CLOCK_OUTPUTS; o++) {
            const ClockOutputCfg* oc = &in->cfg->clock[o];
            if (!oc->enable) continue;
            plan.pulses[o] = clock_output_due(&st->cts[o], bs.beats, oc->ppqn,
                                              oc->phase_mbeats, oc->swing_mbeats, KS_TICK_MAX_BURST);
        }
        // Transport: MIDI Start/Stop on the Link play-state edge (P4-008).
        plan.transport = transport_update(&st->tr, in->start_stop_seen, in->playing);
    }

    // Connected but waiting for transport. The caller pulses the strip; the
    // speaker stays quiet (ESP-009).
    plan.standby = !in->tp_playing;

    // Metronome click, gated by the transport (quiet when stopped, P4-019).
    if (in->cfg->metronome_enable) {
        if (in->tp_playing) {
            MetroClick mc = metronome_update(&st->mt, bs.beats, KS_TICK_METRO_QUANTUM, KS_TICK_METRO_BURST);
            if (mc != METRO_NONE) {
                plan.click        = true;
                plan.click_accent = (mc == METRO_ACCENT) && in->cfg->metronome_accent;
            }
        } else {
            metronome_reset(&st->mt);   // stopped: re-prime on resume, no catch-up burst
        }
    }
    return plan;
}
