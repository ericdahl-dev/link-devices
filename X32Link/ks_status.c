#include "ks_status.h"
#include <stdio.h>

/* Appends with snprintf semantics: `*n` accumulates what WOULD have been written, so a
 * caller can always detect truncation (ks_web serving half a JSON object silently is
 * how a UI just stops updating with no error anywhere). Returns false on encoding error. */
static bool append(char* buf, size_t len, int* n, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));

#include <stdarg.h>
static bool append(char* buf, size_t len, int* n, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int m;
    if ((size_t)*n < len) {
        m = vsnprintf(buf + *n, len - (size_t)*n, fmt, ap);
    } else {
        /* Already full: measure only, never write past the end. */
        char scratch[1];
        m = vsnprintf(scratch, 0, fmt, ap);
    }
    va_end(ap);
    if (m < 0) return false;
    *n += m;
    return true;
}

int ks_status_json(char* buf, size_t len, float bpm, float midi_bpm, int peers, bool usb, uint32_t tx,
                   const char* fw, bool follow_enabled, float follow_bpm, float follow_confidence,
                   bool follow_valid, const int* launch, int launch_count, bool playing, bool link_owns,
                   const WebTickHealth* tick, const LinkPhaseHealth* phase,
                   const char* clk, const uint32_t* pulses, const char* extra) {
    int n = 0;
    if (!append(buf, len,& n,
                "{\"bpm\":%.1f,\"min\":%.1f,\"peers\":%d,\"usb\":%s,\"tx\":%lu,\"fw\":\"%s\","
                "\"follow_enabled\":%s,\"follow_bpm\":%.1f,\"follow_confidence\":%.1f,\"follow_valid\":%s,"
                "\"launch\":[",
                bpm, midi_bpm, peers, usb ? "true" : "false", (unsigned long)tx, fw,
                follow_enabled ? "true" : "false", follow_bpm, follow_confidence,
                follow_valid ? "true" : "false")) return -1;

    /* ESP-029: the array LENGTH is the device's REAL output count -- 4 on KitchenSync,
     * 1 on the Touch. Never padded. A client renders a card per element, so padding a
     * one-output device out to four would draw three dead outputs. */
    for (int i = 0; i < launch_count; i++) {
        if (!append(buf, len, &n, "%s%d", i ? "," : "", launch[i])) return -1;
    }

    if (!append(buf, len, &n, "],\"playing\":%s,\"link_owns\":%s",
                playing ? "true" : "false", link_owns ? "true" : "false")) return -1;

    /* P4-038: the clock task's own health, in X32Link's exact key names (ARC-024) --
     * one shape across the fleet, so one script audits every device. */
    if (tick) {
        if (!append(buf, len, &n,
                    ",\"drop\":%lu,\"burst\":%lu,\"gap\":%lu,\"work\":%lu,\"over\":%lu,\"core\":%d,"
                    "\"w_beats\":%lu,\"w_clock\":%lu,\"reprime\":%lu",
                    (unsigned long)tick->dropped, (unsigned long)tick->bursts,
                    (unsigned long)tick->max_gap_us, (unsigned long)tick->max_work_us,
                    (unsigned long)tick->overruns, tick->core,
                    (unsigned long)tick->w_beats, (unsigned long)tick->w_clock,
                    (unsigned long)tick->reprimes)) return -1;
    }

    /* P4-038: phase health. `xf_step` is the number that moves the BAR LINE -- the
     * GhostXForm is only an origin, and a commit steps it with no slew. */
    if (phase) {
        if (!append(buf, len, &n,
                    ",\"xf\":%lu,\"xf_step\":%lu,\"xf_max\":%lu,\"rtt_min\":%lu,\"rtt_max\":%lu",
                    (unsigned long)phase->commits, (unsigned long)phase->last_step_us,
                    (unsigned long)phase->max_step_us, (unsigned long)phase->rtt_min_us,
                    (unsigned long)phase->rtt_max_us)) return -1;
    }

    /* ESP-028's writer's truth. `clk` is what the WRITER is doing -- locked / free /
     * SILENT -- and `pulses` is the lifetime 0xF8 count, so "is the wire actually
     * alive?" is answerable by polling instead of by logic analyzer. That is the single
     * highest-value diagnostic in the fleet, and the Touch had it while the P4 did not.
     *
     * NULL means ABSENT, never a default: a device that doesn't measure its writer must
     * not claim `clk:"locked"`. That is exactly the lie ESP-028 exists to prevent -- a
     * device reporting `sync:1` over a wire that had been dead for 138 seconds. */
    if (clk) {
        if (!append(buf, len, &n, ",\"clk\":\"%s\"", clk)) return -1;
    }
    if (pulses) {
        if (!append(buf, len, &n, ",\"pulses\":%lu", (unsigned long)*pulses)) return -1;
    }

    /* Device-specific diagnostics, appended INSIDE the object. The Touch's own page reads
     * `cue`, `btn`, `beats` and friends, and those must survive the move to this shared
     * builder. The SHARED keys stay byte-identical across the fleet (ARC-024) while each
     * device can still say more; a client ignores keys it doesn't know, which costs
     * nothing. Caller supplies a bare fragment with no braces and no leading comma. */
    if (extra && extra[0]) {
        if (!append(buf, len, &n, ",%s", extra)) return -1;
    }

    if (!append(buf, len, &n, "}")) return -1;
    return n;
}
