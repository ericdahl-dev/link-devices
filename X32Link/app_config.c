#include "app_config.h"
#include "config.h"
#include <string.h>
#include <stdlib.h>

void config_defaults(AppConfig* cfg) {
    strncpy(cfg->wifi_ssid, DEFAULT_WIFI_SSID, sizeof(cfg->wifi_ssid) - 1);
    cfg->wifi_ssid[sizeof(cfg->wifi_ssid) - 1] = '\0';
    strncpy(cfg->wifi_pass, DEFAULT_WIFI_PASS, sizeof(cfg->wifi_pass) - 1);
    cfg->wifi_pass[sizeof(cfg->wifi_pass) - 1] = '\0';
    strncpy(cfg->mixer_ip,  DEFAULT_MIXER_IP,  sizeof(cfg->mixer_ip)  - 1);
    cfg->mixer_ip[sizeof(cfg->mixer_ip) - 1] = '\0';
    cfg->model          = DEFAULT_MODEL;
    cfg->fx_slot        = DEFAULT_FX_SLOT;
    cfg->input_source   = DEFAULT_INPUT_SOURCE;
    cfg->fdr_enable     = DEFAULT_FDR_ENABLE;
    cfg->fdr_chan_count = DEFAULT_FDR_CHAN_COUNT;
    cfg->quantum_beats  = DEFAULT_QUANTUM_BEATS;
    cfg->midi_clock_out_enable = DEFAULT_MIDI_CLOCK_OUT_ENABLE;
    cfg->phase_display_mode = DEFAULT_PHASE_DISPLAY_MODE;
    cfg->dot_beat_color     = DEFAULT_DOT_BEAT_COLOR;
    cfg->dot_accent_color   = DEFAULT_DOT_ACCENT_COLOR;
}

int config_model_port(int model) {
    return model == MODEL_X32 ? 10023 : 10024;
}

int config_model_slot_max(int model) {
    return model == MODEL_X32 ? 8 : 4;
}

void config_set_model(AppConfig* cfg, int model) {
    if (model != MODEL_XR18 && model != MODEL_X32) return;
    cfg->model = model;
    int max = config_model_slot_max(model);
    if (cfg->fx_slot > max) cfg->fx_slot = max;
    if (cfg->fx_slot < 1)   cfg->fx_slot = 1;
}

// ARC-020: the single owner of "is this persisted blob safe to load?" — see
// app_config.h. Every gate is fail-closed: anything we cannot positively vouch
// for becomes defaults, because loading a stale layout puts garbage into fields
// the user never sees until the mixer gets OSC for the wrong FX slot.
cfg_decode_result config_decode(AppConfig* out, const void* blob, size_t blob_len,
                                bool version_present, uint32_t version) {
    config_defaults(out);

    // Bytes with no version key predate versioning: they could be any old
    // layout, so no size coincidence earns them a load.
    if (!blob || !version_present) return CFG_DECODE_DEFAULTED;

    if (version != APP_CONFIG_VERSION || blob_len != sizeof(AppConfig))
        return CFG_DECODE_DEFAULTED;

    // Version and size both vouch for the layout; the bytes still have to be in
    // range (bit-rot, or a layout shipped without a version bump).
    AppConfig candidate;
    memcpy(&candidate, blob, sizeof(candidate));
    if (!config_validate(&candidate)) return CFG_DECODE_DEFAULTED;

    *out = candidate;
    return CFG_DECODE_OK;
}

bool app_config_set(AppConfig* cfg, AppConfigField field, int value) {
    AppConfig tmp = *cfg;                       // apply to a copy, keep iff it validates
    switch (field) {
        case ACF_INPUT_SOURCE:       tmp.input_source        = value; break;
        case ACF_MODEL:              config_set_model(&tmp, value);   break;
        case ACF_FX_SLOT:            tmp.fx_slot             = value; break;
        case ACF_QUANTUM_BEATS:      tmp.quantum_beats       = value; break;
        case ACF_MIDI_CLOCK_OUT:     tmp.midi_clock_out_enable = value; break;
        case ACF_FDR_ENABLE:         tmp.fdr_enable          = value; break;
        case ACF_FDR_CHAN_COUNT:     tmp.fdr_chan_count      = value; break;
        case ACF_PHASE_DISPLAY_MODE: tmp.phase_display_mode  = value; break;
        case ACF_DOT_BEAT_COLOR:     tmp.dot_beat_color      = value; break;
        case ACF_DOT_ACCENT_COLOR:   tmp.dot_accent_color    = value; break;
        default: return false;
    }
    if (!config_validate(&tmp)) return false;   // config_validate is the one range owner
    *cfg = tmp;
    return true;
}

bool config_validate(const AppConfig* cfg) {
    if (cfg->mixer_ip[0] == '\0') return false;
    if (cfg->fx_slot < 1)                              return false;
    if (cfg->fx_slot > config_model_slot_max(cfg->model)) return false;
    if (cfg->input_source != 0 && cfg->input_source != 1)  return false;
    if (cfg->fdr_enable   != 0 && cfg->fdr_enable   != 1)  return false;
    if (cfg->fdr_chan_count != 16 && cfg->fdr_chan_count != 32) return false;
    if (cfg->quantum_beats < 1 || cfg->quantum_beats > 16)        return false;
    if (cfg->midi_clock_out_enable != 0 && cfg->midi_clock_out_enable != 1) return false;
    if (cfg->phase_display_mode != 0 && cfg->phase_display_mode != 1)       return false;
    if (cfg->dot_beat_color   < 0 || cfg->dot_beat_color   > 0xFFFFFF)      return false;
    if (cfg->dot_accent_color < 0 || cfg->dot_accent_color > 0xFFFFFF)      return false;
    return true;
}

// ESP-040: an HTML <input type=color> posts "#rrggbb"; accept with or without the '#'.
// Rejects anything that isn't exactly six hex digits so a garbled colour never reaches
// the config as a partial parse.
static int parse_hex_color(const char* v, int* out) {
    if (v[0] == '#') v++;
    if (strlen(v) != 6) return 0;
    char* end = NULL;
    long x = strtol(v, &end, 16);
    if (*end != '\0' || x < 0 || x > 0xFFFFFF) return 0;
    *out = (int)x;
    return 1;
}

// ESP-040: bounded copy that treats an EMPTY value as "keep current" (returns true, no
// change) — a form cannot show a password, and X32Link stores exactly one credential, so
// a blank ssid/pass/ip must never wipe it. Rejects an over-long value rather than truncate.
static int keep_or_copy(char* dst, size_t dstsz, const char* v) {
    if (v[0] == '\0') return 1;                 // keep current
    if (strlen(v) >= dstsz) return 0;           // too long — reject, don't truncate
    strncpy(dst, v, dstsz - 1);
    dst[dstsz - 1] = '\0';
    return 1;
}

bool app_config_set_kv(AppConfig* cfg, const char* key, const char* val) {
    // Strings first — they don't route through app_config_set (which is int-only).
    if (strcmp(key, "wifi_ssid") == 0) return keep_or_copy(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), val);
    if (strcmp(key, "wifi_pass") == 0) return keep_or_copy(cfg->wifi_pass, sizeof(cfg->wifi_pass), val);
    if (strcmp(key, "mixer_ip")  == 0) return keep_or_copy(cfg->mixer_ip,  sizeof(cfg->mixer_ip),  val);

    // Colours: the SHARED key (led_beat/led_accent, what /config.json emits) and the
    // device's own form key (dot_beat/dot_acc) are aliases for one field.
    if (strcmp(key, "led_beat") == 0 || strcmp(key, "dot_beat") == 0) {
        int c; if (!parse_hex_color(val, &c)) return false;
        return app_config_set(cfg, ACF_DOT_BEAT_COLOR, c);
    }
    if (strcmp(key, "led_accent") == 0 || strcmp(key, "dot_acc") == 0) {
        int c; if (!parse_hex_color(val, &c)) return false;
        return app_config_set(cfg, ACF_DOT_ACCENT_COLOR, c);
    }

    // Ints via app_config_set, so config_validate stays the one range owner. `clock_out`
    // is the shared key /config.json publishes; `midi_clock_out` is the form's own name.
    if (strcmp(key, "clock_out") == 0 || strcmp(key, "midi_clock_out") == 0)
        return app_config_set(cfg, ACF_MIDI_CLOCK_OUT, atoi(val));
    // A checkbox posts value="1" when on; anything else (or absence) is off.
    if (strcmp(key, "phase_flash") == 0)
        return app_config_set(cfg, ACF_PHASE_DISPLAY_MODE, strcmp(val, "1") == 0 ? 1 : 0);
    if (strcmp(key, "input_source") == 0) return app_config_set(cfg, ACF_INPUT_SOURCE, atoi(val));
    if (strcmp(key, "model") == 0)        return app_config_set(cfg, ACF_MODEL, atoi(val));
    if (strcmp(key, "fx_slot") == 0)      return app_config_set(cfg, ACF_FX_SLOT, atoi(val));
    if (strcmp(key, "quantum") == 0 || strcmp(key, "quantum_beats") == 0)
        return app_config_set(cfg, ACF_QUANTUM_BEATS, atoi(val));
    if (strcmp(key, "fdr_enable") == 0)     return app_config_set(cfg, ACF_FDR_ENABLE, atoi(val));
    if (strcmp(key, "fdr_chan_count") == 0) return app_config_set(cfg, ACF_FDR_CHAN_COUNT, atoi(val));

    return false;   // unknown key — a client that posts garbage changes nothing
}

void x32_form_merge(AppConfig* out, const AppConfig* base,
                    const X32FormField* fields, int n, bool full_form) {
    *out = *base;
    if (full_form) {
        // The device's own page posted the whole form, so an unchecked (absent) checkbox
        // means "off". Pre-clear the two checkbox-backed booleans; a present key below
        // flips its own back on. Value fields (input_source, colours, model, ...) are
        // NEVER cleared by absence — only genuine checkboxes are, which is the whole point.
        out->midi_clock_out_enable = 0;
        out->phase_display_mode    = 0;
    }
    // `fx_slot` validates against the model's slot max (config_validate), so `model` must
    // land FIRST or a valid slot for the NEW model is rejected against the OLD one and
    // silently dropped — independent of the order the fields arrived in the POST. Apply
    // model up front; the main loop re-applies it harmlessly (idempotent).
    for (int i = 0; i < n; i++)
        if (strcmp(fields[i].key, "model") == 0)
            app_config_set_kv(out, fields[i].key, fields[i].val);
    for (int i = 0; i < n; i++)
        app_config_set_kv(out, fields[i].key, fields[i].val);
}
