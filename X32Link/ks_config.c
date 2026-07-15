#include "ks_config.h"
#include <string.h>
#include <stdlib.h>

void ks_config_defaults(KsConfig* c) {
    memset(c, 0, sizeof(*c));
    c->clock_out_enable = 1;
    c->metronome_enable = 0;   // audible: default off so a fresh board is silent
    c->metronome_accent = 1;   // when enabled, accent the bar-1 downbeat
    c->metronome_volume = 80;  // ES8311 codec volume
    c->metronome_voice  = 0;   // Tone (default)
    c->led_enable       = 0;   // visual metronome on the strip: default off
    c->follow_beat_enable = 0;  // mic capture: default off (P4-020)
    c->led_brightness   = 60;  // % (P4-019)
    c->led_mode         = 0;   // chase
    c->led_fade         = 55;  // pulse dim across a beat
    c->led_beat_color   = 0x00B400;   // green
    c->led_accent_color = 0xDC6E00;   // amber
    // P4-010: output 0 is the default 24-PPQN MIDI clock on cable 0; rest off.
    for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) {
        c->clock[i].enable       = (i == 0) ? 1 : 0;
        c->clock[i].cable        = i;
        c->clock[i].ppqn         = 24;
        c->clock[i].phase_mbeats = 0;
        c->clock[i].swing_mbeats = 0;   // straight by default (P4-013)
        c->clock[i].follow_link  = 1;   // Link owns transport by default (ESP-011)
    }
    c->tempo_mbpm = 120000;   // ESP-037: 120.000 BPM (Link default) when solo
}

bool ks_config_valid(const KsConfig* c) {
    if (c->clock_out_enable != 0 && c->clock_out_enable != 1) return false;
    if (c->metronome_enable != 0 && c->metronome_enable != 1) return false;
    if (c->metronome_accent != 0 && c->metronome_accent != 1) return false;
    if (c->metronome_volume < 0 || c->metronome_volume > 100) return false;
    if (c->metronome_voice < 0 || c->metronome_voice > 2) return false;
    if (c->led_enable != 0 && c->led_enable != 1) return false;
    if (c->follow_beat_enable != 0 && c->follow_beat_enable != 1) return false;
    if (c->led_brightness < 0 || c->led_brightness > 100) return false;
    if (c->led_mode < 0 || c->led_mode > 2) return false;
    if (c->led_fade < 0 || c->led_fade > 100) return false;
    if (c->led_beat_color   < 0 || c->led_beat_color   > 0xFFFFFF) return false;
    if (c->led_accent_color < 0 || c->led_accent_color > 0xFFFFFF) return false;
    for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) {
        const ClockOutputCfg* o = &c->clock[i];
        if (o->enable != 0 && o->enable != 1) return false;
        if (o->cable < 0 || o->cable > 3) return false;
        if (o->ppqn < 1 || o->ppqn > 48) return false;
        if (o->phase_mbeats < -250 || o->phase_mbeats > 250) return false;
        if (o->swing_mbeats < 0 || o->swing_mbeats > 250) return false;
        if (o->follow_link != 0 && o->follow_link != 1) return false;
    }
    /* ESP-037: milli-BPM in the musical band (MASTER_CLOCK_BPM_MIN..MAX * 1000).
     * Literals, not the clock header -- this pure config must not pull in Link types. */
    if (c->tempo_mbpm < 20000 || c->tempo_mbpm > 300000) return false;
    return true;
}

// ESP-013: drop the empty slots so the connection policy can stay SSID-blind.
int ks_config_wifi_slots(const KsConfig* c, WifiCred out[KS_WIFI_SLOTS]) {
    int n = 0;
    for (int i = 0; i < KS_WIFI_SLOTS; i++) {
        if (c->wifi[i].ssid[0] != '\0') out[n++] = c->wifi[i];
    }
    return n;
}

// Copy a bounded string; returns false if it wouldn't fit (config unchanged).
static bool copy_field(char* dst, size_t cap, const char* src) {
    if (strlen(src) >= cap) return false;
    strcpy(dst, src);
    return true;
}

// ESP-013 form keys: `base` alone means slot 0 (the pre-multi-network name, kept
// so the existing form and its tests never had to move), `base<N>` means slot N.
// Returns -1 for "not this family, or an out-of-range slot".
static int wifi_slot_index(const char* key, const char* base) {
    size_t n = strlen(base);
    if (strncmp(key, base, n) != 0) return -1;
    const char* suffix = key + n;
    if (suffix[0] == '\0') return 0;
    if (suffix[1] != '\0' || suffix[0] < '0' || suffix[0] > '9') return -1;
    int i = suffix[0] - '0';
    return (i < KS_WIFI_SLOTS) ? i : -1;
}

bool ks_config_set(KsConfig* c, const char* key, const char* value) {
    int slot = wifi_slot_index(key, "wifi_ssid");
    if (slot >= 0) {
        // Empty ssid = forget this network. Clearing the password with it means a
        // forgotten slot can never be retried against a same-named network later.
        if (value[0] == '\0') { memset(&c->wifi[slot], 0, sizeof(c->wifi[slot])); return true; }
        return copy_field(c->wifi[slot].ssid, sizeof(c->wifi[slot].ssid), value);
    }
    slot = wifi_slot_index(key, "wifi_pass");
    if (slot >= 0) {
        if (value[0] == '\0') return true;   // blank = keep current
        return copy_field(c->wifi[slot].pass, sizeof(c->wifi[slot].pass), value);
    }
    if (strcmp(key, "clock_out") == 0) {
        int v = atoi(value);
        if (v != 0 && v != 1) return false;
        c->clock_out_enable = v;
        return true;
    }
    if (strcmp(key, "bpm") == 0) {
        // ESP-037: decimal BPM -> milli-BPM, range-checked. A bad number is rejected
        // (config stays valid) rather than clocking gear at a nonsense rate.
        int mbpm = (int)(atof(value) * 1000.0 + 0.5);
        if (mbpm < 20000 || mbpm > 300000) return false;
        c->tempo_mbpm = mbpm;
        return true;
    }
    if (strcmp(key, "metronome") == 0) {
        int v = atoi(value);
        if (v != 0 && v != 1) return false;
        c->metronome_enable = v;
        return true;
    }
    if (strcmp(key, "metro_accent") == 0) {
        int v = atoi(value);
        if (v != 0 && v != 1) return false;
        c->metronome_accent = v;
        return true;
    }
    if (strcmp(key, "metro_vol") == 0) {
        int v = atoi(value);
        if (v < 0 || v > 100) return false;
        c->metronome_volume = v;
        return true;
    }
    if (strcmp(key, "metro_voice") == 0) {
        int v = atoi(value);
        if (v < 0 || v > 2) return false;
        c->metronome_voice = v;
        return true;
    }
    if (strcmp(key, "led") == 0) {
        int v = atoi(value);
        if (v != 0 && v != 1) return false;
        c->led_enable = v;
        return true;
    }
    if (strcmp(key, "follow_beat") == 0) {
        int v = atoi(value);
        if (v != 0 && v != 1) return false;
        c->follow_beat_enable = v;
        return true;
    }
    if (strcmp(key, "led_bright") == 0) {
        int v = atoi(value);
        if (v < 0 || v > 100) return false;
        c->led_brightness = v;
        return true;
    }
    if (strcmp(key, "led_mode") == 0) {
        int v = atoi(value);
        if (v < 0 || v > 2) return false;
        c->led_mode = v;
        return true;
    }
    if (strcmp(key, "led_fade") == 0) {
        int v = atoi(value);
        if (v < 0 || v > 100) return false;
        c->led_fade = v;
        return true;
    }
    // Colours arrive from an HTML <input type="color"> as "#rrggbb" (the '#' is
    // url-decoded before we see it); accept with or without the leading '#'.
    if (strcmp(key, "led_beat") == 0 || strcmp(key, "led_accent") == 0) {
        const char* hex = (value[0] == '#') ? value + 1 : value;
        char* end = NULL;
        long v = strtol(hex, &end, 16);
        if (end == hex || *end != '\0' || v < 0 || v > 0xFFFFFF) return false;
        if (key[4] == 'b') c->led_beat_color   = (int)v;   // "led_beat"
        else               c->led_accent_color = (int)v;   // "led_accent"
        return true;
    }
    // P4-010 per-output fields: "clk<N>_en|cable|ppqn|phase|swing|follow" (N = 0..3).
    if (strncmp(key, "clk", 3) == 0 && key[3] >= '0' && key[3] <= '9' && key[4] == '_') {
        int idx = key[3] - '0';
        if (idx < 0 || idx >= KS_CLOCK_OUTPUTS) return false;
        const char* f = key + 5;
        int v = atoi(value);
        ClockOutputCfg* o = &c->clock[idx];
        if (strcmp(f, "en") == 0)    { if (v != 0 && v != 1) return false; o->enable = v; return true; }
        if (strcmp(f, "cable") == 0) { if (v < 0 || v > 3)   return false; o->cable = v; return true; }
        if (strcmp(f, "ppqn") == 0)  { if (v < 1 || v > 48)  return false; o->ppqn = v; return true; }
        if (strcmp(f, "phase") == 0) { if (v < -250 || v > 250) return false; o->phase_mbeats = v; return true; }
        if (strcmp(f, "swing") == 0) { if (v < 0 || v > 250)    return false; o->swing_mbeats = v; return true; }
        if (strcmp(f, "follow") == 0){ if (v != 0 && v != 1)    return false; o->follow_link = v; return true; }
        return false;
    }
    return false;
}

// ARC-016: the single owner of the live-safe field set — see ks_config.h.
void ks_config_live_safe_copy(KsConfig* dst, const KsConfig* src) {
    dst->clock_out_enable = src->clock_out_enable;
    dst->metronome_accent = src->metronome_accent;
    dst->metronome_volume = src->metronome_volume;
    dst->metronome_voice  = src->metronome_voice;
    dst->led_enable       = src->led_enable;
    dst->led_brightness   = src->led_brightness;
    dst->led_mode         = src->led_mode;
    dst->led_fade         = src->led_fade;
    dst->led_beat_color   = src->led_beat_color;
    dst->led_accent_color = src->led_accent_color;
    for (int o = 0; o < KS_CLOCK_OUTPUTS; o++) dst->clock[o] = src->clock[o];
}

/* ESP-013: the v1 layout, FROZEN. This is a copy of what shipped, not an alias of
 * the current structs — if ClockOutputCfg or KsConfig ever change again, this must
 * not follow them, or the migration would read v1 bytes at v3 offsets. Nothing here
 * may reference the live types. Its own _Static_assert nails the size. */
typedef struct { int enable, cable, ppqn, phase_mbeats, swing_mbeats; } ClockOutputCfgV1;
typedef struct {
    char wifi_ssid[33];
    char wifi_pass[64];
    int  clock_out_enable, metronome_enable, metronome_accent, metronome_volume, metronome_voice;
    ClockOutputCfgV1 clock[4];
    int  led_enable, led_brightness, led_mode, led_fade, led_beat_color, led_accent_color;
    int  follow_beat_enable;
} KsConfigV1;
_Static_assert(sizeof(KsConfigV1) == 228, "frozen v1 layout must not change (ESP-013 migration)");

// v1 -> v2: the single network becomes slot 0; the new slots start empty.
static void migrate_v1(KsConfig* out, const KsConfigV1* v1) {
    ks_config_defaults(out);            // new fields (wifi[1..]) get their defaults
    memcpy(out->wifi[0].ssid, v1->wifi_ssid, sizeof(out->wifi[0].ssid));
    memcpy(out->wifi[0].pass, v1->wifi_pass, sizeof(out->wifi[0].pass));
    out->wifi[0].ssid[sizeof(out->wifi[0].ssid) - 1] = '\0';   // a corrupt blob need not be NUL-terminated
    out->wifi[0].pass[sizeof(out->wifi[0].pass) - 1] = '\0';
    out->clock_out_enable = v1->clock_out_enable;
    out->metronome_enable = v1->metronome_enable;
    out->metronome_accent = v1->metronome_accent;
    out->metronome_volume = v1->metronome_volume;
    out->metronome_voice  = v1->metronome_voice;
    for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) {
        out->clock[i].enable       = v1->clock[i].enable;
        out->clock[i].cable        = v1->clock[i].cable;
        out->clock[i].ppqn         = v1->clock[i].ppqn;
        out->clock[i].phase_mbeats = v1->clock[i].phase_mbeats;
        out->clock[i].swing_mbeats = v1->clock[i].swing_mbeats;
    }
    out->led_enable        = v1->led_enable;
    out->led_brightness    = v1->led_brightness;
    out->led_mode          = v1->led_mode;
    out->led_fade          = v1->led_fade;
    out->led_beat_color    = v1->led_beat_color;
    out->led_accent_color  = v1->led_accent_color;
    out->follow_beat_enable = v1->follow_beat_enable;
}

// ESP-037: the v3 layout, FROZEN -- the KsConfig that shipped right before tempo. It is
// byte-identical to the first (sizeof(KsConfig)-4) bytes of the live v4 struct, since
// tempo_mbpm was appended at the end, so the migration is a copy plus the one default.
// The size assert is the tripwire: if the live ClockOutputCfg/WifiCred ever change, this
// stops being 436 and the build breaks here instead of silently reading a P4's bytes wrong.
typedef struct {
    WifiCred wifi[KS_WIFI_SLOTS];
    int  clock_out_enable, metronome_enable, metronome_accent, metronome_volume, metronome_voice;
    ClockOutputCfg clock[KS_CLOCK_OUTPUTS];
    int  led_enable, led_brightness, led_mode, led_fade, led_beat_color, led_accent_color;
    int  follow_beat_enable;
} KsConfigV3;
_Static_assert(sizeof(KsConfigV3) == 436, "frozen v3 layout must not change (ESP-037 migration)");

// v3 -> v4: every field carries across; the new tempo comes up at its default (120 BPM),
// so a migrated P4 free-runs at 120 exactly as it did before tempo was settable.
static void migrate_v3(KsConfig* out, const KsConfigV3* v3) {
    ks_config_defaults(out);            // tempo_mbpm -> 120000, then the v3 bytes overwrite the rest
    memcpy(out, v3, sizeof(*v3));        // v3 layout == first 436 bytes of KsConfig
    out->tempo_mbpm = 120000;           // the one field v3 never had
}

// P4-014: the single owner of "is this persisted blob safe to load?" — see ks_config.h.
// Every gate is fail-closed: anything we can't positively vouch for becomes defaults,
// because loading a stale layout puts garbage in fields the user never sees until a
// clock output misfires.
ks_decode_result ks_config_decode(KsConfig* out, const void* blob, size_t blob_len,
                                  bool version_present, uint32_t version) {
    ks_config_defaults(out);

    // A legacy blob predates the version key: its bytes could be any old layout,
    // so no size coincidence earns it a migration.
    if (!blob || !version_present) return KS_DECODE_DEFAULTED;

    if (version == 1u && blob_len == sizeof(KsConfigV1)) {
        KsConfigV1 v1;
        memcpy(&v1, blob, sizeof(v1));
        KsConfig candidate;
        migrate_v1(&candidate, &v1);
        // A migration is not a licence to skip the guard.
        if (!ks_config_valid(&candidate)) return KS_DECODE_DEFAULTED;
        *out = candidate;
        return KS_DECODE_MIGRATED;
    }

    if (version == 3u && blob_len == sizeof(KsConfigV3)) {   // ESP-037
        KsConfigV3 v3;
        memcpy(&v3, blob, sizeof(v3));
        KsConfig candidate;
        migrate_v3(&candidate, &v3);
        if (!ks_config_valid(&candidate)) return KS_DECODE_DEFAULTED;
        *out = candidate;
        return KS_DECODE_MIGRATED;
    }

    if (version != KS_CONFIG_VERSION || blob_len != sizeof(KsConfig)) return KS_DECODE_DEFAULTED;

    // Version and size both vouch for the layout; the bytes still have to be in
    // range (bit-rot, or a layout change someone shipped without bumping).
    KsConfig candidate;
    memcpy(&candidate, blob, sizeof(candidate));
    if (!ks_config_valid(&candidate)) return KS_DECODE_DEFAULTED;

    *out = candidate;
    return KS_DECODE_OK;
}
