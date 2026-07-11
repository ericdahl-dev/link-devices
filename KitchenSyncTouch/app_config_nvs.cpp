// NVS persistence for the KitchenSync Touch config (ESP-016). Namespace "kstouch"
// -- deliberately NOT "x32link": a board reflashed from X32Link must not read its
// mixer/model keys into this trimmed struct.
#include "app_config.h"
#include <Preferences.h>

static Preferences prefs;

extern "C" void config_load(AppConfig* cfg) {
    config_defaults(cfg);
    prefs.begin("kstouch", true);   // read-only
    cfg->quantum_beats    = prefs.getInt("quantum",   cfg->quantum_beats);
    cfg->clock_enable     = prefs.getInt("clock_en",  cfg->clock_enable);
    cfg->transport_enable = prefs.getInt("tport_en",  cfg->transport_enable);
    cfg->play_on_release  = prefs.getInt("play_rel",  cfg->play_on_release);
    cfg->nudge_mbeats     = prefs.getInt("nudge_mb",  cfg->nudge_mbeats);
    cfg->brightness       = prefs.getInt("bright",    cfg->brightness);
    prefs.getString("wifi_ssid", cfg->wifi_ssid, sizeof(cfg->wifi_ssid));
    prefs.getString("wifi_pass", cfg->wifi_pass, sizeof(cfg->wifi_pass));
    prefs.end();
}

extern "C" void config_save(const AppConfig* cfg) {
    prefs.begin("kstouch", false);  // read-write
    prefs.putInt("quantum",  cfg->quantum_beats);
    prefs.putInt("clock_en", cfg->clock_enable);
    prefs.putInt("tport_en", cfg->transport_enable);
    prefs.putInt("play_rel", cfg->play_on_release);
    prefs.putInt("nudge_mb", cfg->nudge_mbeats);
    prefs.putInt("bright",   cfg->brightness);
    prefs.putString("wifi_ssid", cfg->wifi_ssid);
    prefs.putString("wifi_pass", cfg->wifi_pass);
    prefs.end();
}
