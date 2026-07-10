// See web_status_json.h.
#include "web_status_json.h"
#include <stdio.h>

int web_status_json(char* buf, size_t buf_len, float bpm, float phase, bool valid, int quantum,
                    const char* fw, bool has_batt, float batt_v, float batt_pct) {
    if (!has_batt) {
        return snprintf(buf, buf_len,
            "{\"bpm\":%.1f,\"phase\":%.4f,\"valid\":%s,\"quantum\":%d,\"fw\":\"%s\"}",
            (double)bpm, (double)phase, valid ? "true" : "false", quantum, fw);
    }
    return snprintf(buf, buf_len,
        "{\"bpm\":%.1f,\"phase\":%.4f,\"valid\":%s,\"quantum\":%d,\"fw\":\"%s\","
        "\"batt_v\":%.2f,\"batt_pct\":%.1f}",
        (double)bpm, (double)phase, valid ? "true" : "false", quantum, fw,
        (double)batt_v, (double)batt_pct);
}
