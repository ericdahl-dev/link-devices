// See web_status_json.h.
#include "web_status_json.h"
#include <stdarg.h>
#include <stdio.h>

// Appended as a cursor rather than one snprintf per combination: there are now two
// independent optional blocks (battery, ARC-024 tick health), and a branch per
// combination is how you end up with a well-formed object in three cases and a
// malformed one in the fourth. Each append is snprintf-bounded and the running total
// is snprintf-style, so truncation is still reported exactly as before.
static void app(char** p, size_t* left, int* total, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));

static void app(char** p, size_t* left, int* total, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(*p, *left, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    *total += n;
    // On truncation snprintf returns what it WOULD have written; advance only by what
    // it actually did, and never past the end of the buffer.
    size_t adv = ((size_t)n < *left) ? (size_t)n : (*left ? *left - 1 : 0);
    *p    += adv;
    *left -= adv;
}

int web_status_json(char* buf, size_t buf_len, float bpm, float phase, bool valid, int quantum,
                    const char* fw, bool has_batt, float batt_v, float batt_pct,
                    const WebTickHealth* tick) {
    char*  p     = buf;
    size_t left  = buf_len;
    int    total = 0;

    app(&p, &left, &total, "{\"bpm\":%.1f,\"phase\":%.4f,\"valid\":%s,\"quantum\":%d,\"fw\":\"%s\"",
        (double)bpm, (double)phase, valid ? "true" : "false", quantum, fw);

    if (has_batt)
        app(&p, &left, &total, ",\"batt_v\":%.2f,\"batt_pct\":%.1f",
            (double)batt_v, (double)batt_pct);

    if (tick)
        app(&p, &left, &total,
            ",\"drop\":%lu,\"burst\":%lu,\"gap\":%lu,\"work\":%lu,\"over\":%lu,\"core\":%d,"
            "\"w_beats\":%lu,\"w_clock\":%lu,\"reprime\":%lu",
            (unsigned long)tick->dropped, (unsigned long)tick->bursts,
            (unsigned long)tick->max_gap_us, (unsigned long)tick->max_work_us,
            (unsigned long)tick->overruns, tick->core,
            (unsigned long)tick->w_beats, (unsigned long)tick->w_clock,
            (unsigned long)tick->reprimes);

    app(&p, &left, &total, "}");
    return total;
}
