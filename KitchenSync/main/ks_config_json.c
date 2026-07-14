#include "ks_config_json.h"
#include <stdio.h>
#include <stdbool.h>

// Appends one snprintf's worth of text at n, tracking truncation the same way
// ks_status_json()'s chain does: once a step is truncated (or a prior step
// already was), stop calling snprintf again -- its return value on a truncated
// buffer is "what would have fit", not "how much I wrote", so writing further
// from a wrong offset would corrupt what's already in buf.
#define APPEND(...)                                                          \
    do {                                                                     \
        if (truncated) break;                                               \
        int _m = snprintf(buf + n, (n < len) ? (len - n) : 0, __VA_ARGS__);  \
        if (_m < 0) { truncated = true; break; }                             \
        n += (size_t)_m;                                                     \
        if (n >= len) truncated = true;                                      \
    } while (0)

int ks_config_json(char* buf, size_t len, const KsConfig* c) {
    size_t n = 0;
    bool truncated = false;

    APPEND("{\"clock_out\":%s,\"metronome\":%s,\"metro_accent\":%s,\"metro_vol\":%d,"
           "\"metro_voice\":%d,\"led\":%s,\"led_bright\":%d,\"led_mode\":%d,\"led_fade\":%d,"
           "\"led_beat\":\"#%06X\",\"led_accent\":\"#%06X\",\"follow_beat\":%s,\"wifi\":[",
           c->clock_out_enable ? "true" : "false", c->metronome_enable ? "true" : "false",
           c->metronome_accent ? "true" : "false", c->metronome_volume, c->metronome_voice,
           c->led_enable ? "true" : "false", c->led_brightness, c->led_mode, c->led_fade,
           (unsigned)(c->led_beat_color & 0xFFFFFF), (unsigned)(c->led_accent_color & 0xFFFFFF),
           c->follow_beat_enable ? "true" : "false");

    for (int i = 0; i < KS_WIFI_SLOTS && !truncated; i++) {
        APPEND("%s{\"ssid\":\"%s\",\"pass_set\":%s}", i ? "," : "", c->wifi[i].ssid,
               c->wifi[i].pass[0] ? "true" : "false");
    }
    APPEND("],\"clock\":[");
    for (int i = 0; i < KS_CLOCK_OUTPUTS && !truncated; i++) {
        const ClockOutputCfg* o = &c->clock[i];
        APPEND("%s{\"en\":%s,\"cable\":%d,\"ppqn\":%d,\"phase\":%d,\"swing\":%d,\"follow\":%s}",
               i ? "," : "", o->enable ? "true" : "false", o->cable, o->ppqn, o->phase_mbeats,
               o->swing_mbeats, o->follow_link ? "true" : "false");
    }
    APPEND("]}");

    // snprintf-style: the accumulated length even when truncated (ks_status_json's
    // same convention), so a caller can compare it against `len` to detect
    // truncation and, if it cares, retry with a bigger buffer.
    return (int)n;
}

#undef APPEND
