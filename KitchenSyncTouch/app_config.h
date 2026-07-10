#pragma once
// KitchenSync Touch runtime config (ESP-016). Trimmed from X32Link's AppConfig:
// NO mixer_ip / model / fx_slot / input_source / fdr -- this is a MIDI product,
// Link-only. config_validate/app_config_set + NVS land with the web form (Inc3).
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char wifi_ssid[64];
    char wifi_pass[64];
    int  quantum_beats;      // beats per bar for phase / launch quantize (LNK-019)
    int  clock_enable;       // 0/1 — emit 24-PPQN MIDI clock on DIN + USB
    int  transport_enable;   // 0/1 — allow PLAY/STOP from the touch screen (Inc2)
} AppConfig;

void config_defaults(AppConfig* cfg);

#ifdef __cplusplus
}
#endif
