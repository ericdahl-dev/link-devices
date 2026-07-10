#include "app_config.h"
#include "config.h"
#include <string.h>

void config_defaults(AppConfig* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->quantum_beats    = DEFAULT_QUANTUM_BEATS;
    cfg->clock_enable     = DEFAULT_CLOCK_ENABLE;
    cfg->transport_enable = DEFAULT_TRANSPORT_ENABLE;
    cfg->play_on_release  = DEFAULT_PLAY_ON_RELEASE;
}

static bool is_bool(int v) { return v == 0 || v == 1; }

bool config_validate(const AppConfig* cfg) {
    if (cfg->quantum_beats < 1 || cfg->quantum_beats > 16) return false;
    if (!is_bool(cfg->clock_enable))     return false;
    if (!is_bool(cfg->transport_enable)) return false;
    if (!is_bool(cfg->play_on_release))  return false;
    return true;   // empty ssid is valid (AP setup mode); caller checks it separately
}

bool app_config_set(AppConfig* cfg, AppConfigField field, int value) {
    AppConfig t = *cfg;
    switch (field) {
        case ACF_QUANTUM_BEATS:    t.quantum_beats    = value; break;
        case ACF_CLOCK_ENABLE:     t.clock_enable     = value; break;
        case ACF_TRANSPORT_ENABLE: t.transport_enable = value; break;
        case ACF_PLAY_ON_RELEASE:  t.play_on_release  = value; break;
        default: return false;
    }
    if (!config_validate(&t)) return false;   // keep the change only if it validates
    *cfg = t;
    return true;
}
