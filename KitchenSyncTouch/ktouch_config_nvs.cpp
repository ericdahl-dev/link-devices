// ESP-042: NVS persistence for the Touch's config. The Touch converged onto the shared
// KsConfig (retiring AppConfig), so this stores a KsConfig blob under the "kstouch"
// namespace -- deliberately NOT "x32link": a board reflashed from X32Link must not read
// its mixer/model keys into this box.
//
// The config is ONE versioned blob. This file does no judging of its own: it hands
// whatever NVS held to the pure, host-tested deciders (ks_config_decode for a native
// KsConfig blob, ktouch_migrate_legacy_config for an old AppConfig blob) and stores
// whatever they return. Adding a config field touches the struct/defaults/validate/set
// and ZERO lines here.
#include "ktouch_config_nvs.h"
#include "ktouch_config_migrate.h"
#include "ks_config.h"
#include <Preferences.h>
#include <string.h>

static Preferences prefs;

#define NS      "kstouch"
#define KEY_CFG "cfg"
#define KEY_VER "ver"

// The pre-ARC-020 field-keyed layout (never a blob, so the pure deciders can't see it).
// Read ONCE, on the first boot after this upgrade, then written back as a KsConfig blob so
// it never runs again. A field unit (Dan's Touch, ESP-021) that somehow still holds these
// keys would otherwise lose its WiFi on the way to the converged firmware. The old keys
// map straight onto KsConfig; anything the old build never wrote keeps the KsConfig
// default (which equals the old DEFAULT_* by construction).
static bool migrate_legacy_keys(KsConfig* cfg) {
    if (!prefs.isKey("quantum") && !prefs.isKey("wifi_ssid") &&
        !prefs.isKey("bright")  && !prefs.isKey("nudge_mb")) {
        return false;              // fresh flash, nothing to migrate
    }

    ks_config_defaults(cfg);       // any key the old build never wrote keeps its default
    cfg->quantum_beats        = prefs.getInt("quantum",   cfg->quantum_beats);
    cfg->clock_out_enable     = prefs.getInt("clock_en",  cfg->clock_out_enable);
    cfg->transport_enable     = prefs.getInt("tport_en",  cfg->transport_enable);
    cfg->play_on_release      = prefs.getInt("play_rel",  cfg->play_on_release);
    cfg->clock[0].phase_mbeats = prefs.getInt("nudge_mb", cfg->clock[0].phase_mbeats);
    cfg->lcd_brightness       = prefs.getInt("bright",    cfg->lcd_brightness);
    // The legacy keys held ONE credential; it becomes slot 0.
    prefs.getString("wifi_ssid", cfg->wifi[0].ssid, sizeof(cfg->wifi[0].ssid));
    prefs.getString("wifi_pass", cfg->wifi[0].pass, sizeof(cfg->wifi[0].pass));

    // A migration is not a licence to skip the guard: garbage in the old keys is still
    // garbage. Same gate the blob path runs through.
    return ks_config_valid(cfg);
}

extern "C" void config_load(KsConfig* cfg) {
    uint8_t  buf[sizeof(KsConfig)];
    size_t   sz        = 0;
    uint32_t ver       = 0;
    bool     have_ver  = false;
    bool     have_blob = false;

    memset(buf, 0, sizeof(buf));

    prefs.begin(NS, true);   // read-only
    have_ver = prefs.isKey(KEY_VER);
    if (have_ver) ver = prefs.getUInt(KEY_VER, 0);

    // Read the stored length FIRST, then read anything that FITS the buffer -- a wrong
    // size is rejected by the decider, never truncated into a plausible half-copy. An old
    // AppConfig blob (152..328 bytes) is smaller than KsConfig, so it fits and is read;
    // the decider judges it by (version, size).
    size_t stored = prefs.getBytesLength(KEY_CFG);
    if (stored > 0) {
        have_blob = true;
        sz = stored;
        if (stored <= sizeof(buf)) prefs.getBytes(KEY_CFG, buf, stored);
        // else: bigger than any layout we know -- leave buf zeroed; the deciders reject
        // on size before reading a byte.
    }
    prefs.end();

    const void* blob = have_blob ? (const void*)buf : NULL;

    // KS_CONFIG_VERSION (>= 5) is a NATIVE KsConfig blob -- the P4's own decode lineage,
    // including its v4->v5 in-place migration. A version of 1..4 in THIS namespace is a
    // legacy Touch AppConfig blob (the Touch never stored a KsConfig before), migrated
    // once by ktouch_migrate_legacy_config and written back as a v5 KsConfig.
    ks_decode_result r;
    if (have_ver && ver >= KS_CONFIG_VERSION) {
        r = ks_config_decode(cfg, blob, sz, have_ver, ver);
    } else if (have_ver && ver >= 1u && ver <= 4u &&
               ktouch_migrate_legacy_config(cfg, blob, sz, ver)) {
        r = KS_DECODE_MIGRATED;
    } else {
        ks_config_defaults(cfg);
        r = KS_DECODE_DEFAULTED;
    }

    if (r == KS_DECODE_OK) return;               // common path: native blob accepted

    // An OLD blob (AppConfig, or an older KsConfig on a hypothetical future downgrade path)
    // was upgraded. WRITE IT BACK NOW as a v5 KsConfig -- otherwise NVS keeps the old bytes
    // and every future boot re-migrates.
    if (r == KS_DECODE_MIGRATED) {
        config_save(cfg);
        return;
    }

    // Defaults are already in *cfg. If there is no blob but the pre-ARC-020 field-keys are
    // present, this is the first boot after that upgrade -- migrate them once and write the
    // blob back so the next boot takes the fast path and the legacy keys are never read.
    if (!have_blob) {
        prefs.begin(NS, true);
        bool migrated = migrate_legacy_keys(cfg);
        prefs.end();
        if (migrated) config_save(cfg);
    }
}

extern "C" void config_save(const KsConfig* cfg) {
    prefs.begin(NS, false);  // read-write
    // Blob and version are written together, so a power cut cannot leave a version stamp
    // vouching for a blob that was never written (or vice versa).
    prefs.putBytes(KEY_CFG, cfg, sizeof(*cfg));
    prefs.putUInt(KEY_VER, KS_CONFIG_VERSION);
    prefs.end();
}
