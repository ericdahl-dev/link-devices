#include "app_config.h"
#include "config.h"
#include <string.h>

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
