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
