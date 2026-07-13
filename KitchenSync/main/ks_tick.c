// Pure clock-tick orchestration — see ks_tick.h (ARC-015).
#include "ks_tick.h"
#include <string.h>

void ks_tick_reset(KsTickState* st, uint32_t cfg_gen) {
    beat_source_reset(&st->src);
    for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) clock_ticker_reset(&st->cts[i]);
    metronome_reset(&st->mt);
    for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) {
        transport_reset(&st->tr[i]);
        transport_launch_reset(&st->tl[i]);
    }
    bar_reset_reset(&st->bar);
    st->seen_gen = cfg_gen;
    st->had_stst = false;
    st->held_playing = false;
}

KsTickPlan ks_tick_step(KsTickState* st, const KsTickInputs* in) {
    KsTickPlan plan;
    memset(&plan, 0, sizeof(plan));
    for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) plan.transport[i] = TRANSPORT_NONE;

    // beat_source (ARC-007) owns the basis: phase-locked session beat once a
    // GhostXForm is committed vs the free-running local accumulator, plus the
    // re-prime signal on any basis switch or session loss.
    BeatSourceOut bs = beat_source_step(&st->src, in->have_session, in->xform, in->tl, in->t_now);
    plan.active = bs.active;
    plan.locked = bs.locked;
    plan.beats  = bs.beats;
    // P4-039: derive from the settled timeline, which survives peer loss —
    // not link_proto_bpm(), which a caller may zero independently of it.
    plan.bpm    = (bs.active && in->tl.micros_per_beat > 0)
                    ? (float)(60.0e6 / (double)in->tl.micros_per_beat) : 0.0f;

    // P4-039: latch the last known transport-playing state once the session has
    // ever reported one. A peer-loss reset of start_stop_seen (link_protocol.c)
    // must not snap this back to false — that's what made /status lie about a
    // clock that was still running.
    if (in->start_stop_seen) { st->had_stst = true; st->held_playing = in->playing; }
    plan.playing = st->had_stst ? st->held_playing : in->playing;

    // Reprime fold: a basis switch/session loss (bs.reprime) OR a live config edit
    // (P4-015: g_cfg_gen bumped) shifts the beat grid — re-prime the tick + click
    // grids so the boundary realigns instead of dumping a catch-up burst. On the
    // session-loss edge also reset transport so a re-join doesn't fire a spurious Start.
    bool reprime = bs.reprime;
    if (in->cfg_gen != st->seen_gen) { reprime = true; st->seen_gen = in->cfg_gen; }
    if (reprime) {
        for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) clock_ticker_reset(&st->cts[i]);
        metronome_reset(&st->mt);
        bar_reset_reset(&st->bar);   // a re-origin must not fire a false downbeat
        if (!bs.active) for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) transport_reset(&st->tr[i]);
    }
    plan.reprime = reprime;

    if (!bs.active) {
        for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) plan.launch_state[i] = st->tl[i].state;
        return plan;
    }

    // Clock fan-out: the one shared beat to each enabled output at its own division +
    // phase nudge + swing (P4-010), gated by the master clock switch. NOT gated on
    // the USB host: how many pulses are due is a musical decision, and the DIN MIDI
    // output (ESP-015) must clock whether or not a USB device is attached. The glue
    // gates only the USB *send* on usb_ready; the pulse count is computed regardless.
    if (in->cfg->clock_out_enable) {
        for (int o = 0; o < KS_CLOCK_OUTPUTS; o++) {
            const ClockOutputCfg* oc = &in->cfg->clock[o];
            if (!oc->enable) continue;
            plan.pulses[o] = clock_output_due(&st->cts[o], bs.beats, oc->ppqn,
                                              oc->phase_mbeats, oc->swing_mbeats, KS_TICK_MAX_BURST);
        }
    }

    // Transport runs whether or not a USB device is attached: pulses need the
    // cable, transport STATE does not. (Found on hardware -- with the launch
    // step inside the usb_ready gate, pressing Play with nothing plugged in
    // never even armed.) Only the emitted packets are dropped downstream.
    if (in->cfg->clock_out_enable) {
        // Transport, per output (ESP-011). Two sources, one wire:
        //   - the web UI's launch intent, quantized to the bar line by `tl`,
        //   - the Link session's play state (P4-008), which still drives every
        //     output when the session publishes transport.
        // `tr` sits downstream of both and emits exactly one 0xFA/0xFC per
        // transition, so the two can never double-fire.
        // Arbitration, PER OUTPUT: exactly one master owns each output's transport.
        // Link owns an output once the session publishes StartStopState AND that
        // output is set to follow it (cfg.clock[o].follow_link, the default); such
        // an output ignores manual presses and the UI greys its toggle. An output
        // with follow_link=0 is manual: its web toggle drives it and the session's
        // transport never touches it, so a drum machine can follow Ableton while a
        // synth is launched by hand two bars later (ESP-011's whole point).
        //
        // Never BOTH. Two masters is how "why did my gear stop" happens -- and it
        // did, on the bench: a manual START was stomped by the session's stopped
        // state in the same millisecond. Picking the master per output is what
        // keeps that impossible while still allowing independent outputs.
        bool session_playing = in->start_stop_seen;

        for (int o = 0; o < KS_CLOCK_OUTPUTS; o++) {
            if (!in->cfg->clock[o].enable) continue;
            bool session_owns = session_playing && in->cfg->clock[o].follow_link;
            TransportLaunchIntent intent = session_owns ? TL_INTENT_NONE : in->launch[o];
            TransportLaunchOut lo = transport_launch_step(&st->tl[o], intent,
                                                          bs.beats, KS_TICK_METRO_QUANTUM, bs.active);
            if (session_owns) {
                plan.transport[o] = transport_update(&st->tr[o], true, in->playing);
                continue;
            }
            if (lo.action != TL_NONE) {
                // Manual launch bypasses `tr`: transport_launch already fires at
                // most once per transition, and feeding it through transport_update
                // would be swallowed by that module's priming rule (the first valid
                // observation records state and emits nothing -- deliberate, so
                // joining a session mid-play doesn't jump gear to bar 1, P4-008).
                // Sync `tr` so a later session edge doesn't re-emit what we just sent.
                //
                // An explicit switch, NOT `(action == TL_START) ? START : STOP`. That
                // ternary silently mapped every non-START action to STOP, which was fine
                // while START and STOP were the only two -- and became a trap the moment
                // ESP-025 added TL_RESTART: the P4 would have emitted a bare Stop where a
                // restart was asked for, on the bar line, with nothing in any log.
                switch (lo.action) {
                    case TL_START:
                        plan.transport[o] = TRANSPORT_START;
                        st->tr[o].primed  = true;
                        st->tr[o].playing = true;
                        break;
                    case TL_STOP:
                        plan.transport[o] = TRANSPORT_STOP;
                        st->tr[o].primed  = true;
                        st->tr[o].playing = false;
                        break;
                    case TL_RESTART:
                        // Unreachable today: the P4 posts no TL_INTENT_REALIGN (only the
                        // ESP-025 bench rig does). Supporting it here needs a plan-level
                        // change -- a restart is TWO bytes (0xFC then 0xFA) in one tick,
                        // and plan.transport[] holds exactly one action per output. Left
                        // as an explicit no-op rather than silently degrading to Stop.
                        break;
                    case TL_NONE:
                    default:
                        break;
                }
            }
        }
    }
    for (int o = 0; o < KS_CLOCK_OUTPUTS; o++) plan.launch_state[o] = st->tl[o].state;

    // ESP-015: once per bar boundary. The BarReset tracker owns "did we cross into
    // a new bar", exactly the primitive the analog reset pulse (P4-021) reuses.
    // Called once per active tick because it mutates state.
    plan.downbeat = bar_reset_due(&st->bar, bs.beats, KS_TICK_METRO_QUANTUM);

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
