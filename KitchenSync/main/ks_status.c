#include "ks_status.h"
#include <stdio.h>

int ks_status_json(char* buf, size_t len, float bpm, float midi_bpm, int peers, bool usb, uint32_t tx,
                   const char* fw, bool follow_enabled, float follow_bpm, float follow_confidence,
                   bool follow_valid, const int launch[4], bool playing, bool link_owns,
                   const WebTickHealth* tick) {
    int n = snprintf(buf, len,
                     "{\"bpm\":%.1f,\"min\":%.1f,\"peers\":%d,\"usb\":%s,\"tx\":%lu,\"fw\":\"%s\","
                     "\"follow_enabled\":%s,\"follow_bpm\":%.1f,\"follow_confidence\":%.1f,\"follow_valid\":%s,"
                     "\"launch\":[%d,%d,%d,%d],\"playing\":%s,\"link_owns\":%s",
                     bpm, midi_bpm, peers, usb ? "true" : "false", (unsigned long)tx, fw,
                     follow_enabled ? "true" : "false", follow_bpm, follow_confidence,
                     follow_valid ? "true" : "false",
                     launch[0], launch[1], launch[2], launch[3],
                     playing ? "true" : "false", link_owns ? "true" : "false");
    if (n < 0) return n;

    /* P4-038: the clock task's own health, in X32Link's exact key names (ARC-024) --
     * one shape across the fleet, so one script audits every device. */
    if (tick && (size_t)n < len) {
        int m = snprintf(buf + n, len - (size_t)n,
                         ",\"drop\":%lu,\"burst\":%lu,\"gap\":%lu,\"work\":%lu,\"over\":%lu,\"core\":%d,"
                         "\"w_beats\":%lu,\"w_clock\":%lu,\"reprime\":%lu",
                         (unsigned long)tick->dropped, (unsigned long)tick->bursts,
                         (unsigned long)tick->max_gap_us, (unsigned long)tick->max_work_us,
                         (unsigned long)tick->overruns, tick->core,
                         (unsigned long)tick->w_beats, (unsigned long)tick->w_clock,
                         (unsigned long)tick->reprimes);
        if (m < 0) return m;
        n += m;
    }

    if ((size_t)n < len) {
        int m = snprintf(buf + n, len - (size_t)n, "}");
        if (m < 0) return m;
        n += m;
    } else {
        n += 1;
    }
    return n;
}
