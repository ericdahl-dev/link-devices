// NVS persistence for the KitchenSync Touch config (ESP-016). Namespace "kstouch"
// -- deliberately NOT "x32link": a board reflashed from X32Link must not read its
// mixer/model keys into this trimmed struct.
//
// ARC-020: the config is ONE versioned blob, not a field-by-field key list. This
// file does no judging of its own -- it hands whatever NVS held to the pure
// config_decode() (app_config.c, host-tested) and stores whatever it is given.
// Adding a config field now touches struct/defaults/validate/set and ZERO lines
// here, which is the entire point: the old code listed every field twice, and a
// missed put/get pair failed to persist silently.
#include "app_config.h"
#include <Preferences.h>
#include <string.h>

static Preferences prefs;

#define NS      "kstouch"
#define KEY_CFG "cfg"
#define KEY_VER "ver"

// The pre-ARC-020 field-keyed layout. Read ONCE, on the first boot after the
// upgrade, then written back as a blob so this never runs again.
//
// This cannot live in the pure config_decode(): the old format is not a blob, it
// is a set of Preferences keys, so only this file can see it. A customer unit
// (Dan's Touch, ESP-021) OTAs across this change -- stranding its saved WiFi
// creds and nudge trim would be a silent, unrecoverable-in-the-field reset.
static bool migrate_legacy(AppConfig* cfg) {
    if (!prefs.isKey("quantum") && !prefs.isKey("wifi_ssid") &&
        !prefs.isKey("bright")  && !prefs.isKey("nudge_mb")) {
        return false;              // fresh flash, nothing to migrate
    }

    AppConfig legacy;
    config_defaults(&legacy);      // any key the old build never wrote keeps its default
    legacy.quantum_beats    = prefs.getInt("quantum",   legacy.quantum_beats);
    legacy.clock_enable     = prefs.getInt("clock_en",  legacy.clock_enable);
    legacy.transport_enable = prefs.getInt("tport_en",  legacy.transport_enable);
    legacy.play_on_release  = prefs.getInt("play_rel",  legacy.play_on_release);
    legacy.nudge_mbeats     = prefs.getInt("nudge_mb",  legacy.nudge_mbeats);
    legacy.brightness       = prefs.getInt("bright",    legacy.brightness);
    // ESP-030: the legacy keys held ONE credential; it becomes slot 0.
    prefs.getString("wifi_ssid", legacy.wifi[0].ssid, sizeof(legacy.wifi[0].ssid));
    prefs.getString("wifi_pass", legacy.wifi[0].pass, sizeof(legacy.wifi[0].pass));

    // A migration is not a licence to skip the guard: garbage in the old keys is
    // still garbage. Same gate the blob path runs through.
    if (!config_validate(&legacy)) return false;

    *cfg = legacy;
    return true;
}

extern "C" void config_load(AppConfig* cfg) {
    uint8_t  buf[sizeof(AppConfig)];
    size_t   sz        = 0;
    uint32_t ver       = 0;
    bool     have_ver  = false;
    bool     have_blob = false;

    memset(buf, 0, sizeof(buf));

    prefs.begin(NS, true);   // read-only
    have_ver = prefs.isKey(KEY_VER);
    if (have_ver) ver = prefs.getUInt(KEY_VER, 0);

    // Read the stored length FIRST. getBytes into a smaller buffer would silently
    // truncate; handing config_decode() the real length instead lets it reject a
    // wrong-size blob rather than load a half-copy.
    //
    // ESP-030: this used to read the bytes ONLY when `stored == sizeof(buf)` — an
    // exact match against the CURRENT struct. That was safe while there was one
    // layout, and became a config-eating bug the moment an OLD one existed: a v1
    // blob is 152 bytes and the buffer is now 316, so the bytes would never be
    // read, `buf` would stay zeroed, and the migration would faithfully migrate
    // 152 bytes of ZEROES — silently wiping the WiFi credentials of every device
    // in the field, including the customer unit (ESP-021). Read anything that
    // FITS; let config_decode judge it.
    size_t stored = prefs.getBytesLength(KEY_CFG);
    if (stored > 0) {
        have_blob = true;
        sz = stored;
        if (stored <= sizeof(buf)) prefs.getBytes(KEY_CFG, buf, stored);
        // else: bigger than any layout we know — leave buf zeroed; decode rejects
        // on size before it reads a byte.
    }
    prefs.end();

    cfg_decode_result r = config_decode(cfg, have_blob ? (const void*)buf : NULL,
                                        sz, have_ver, ver);

    if (r == CFG_DECODE_OK) return;              // the common path: blob accepted

    // ESP-030: an OLD blob was upgraded. WRITE IT BACK NOW — otherwise NVS keeps the
    // v1 bytes and every future boot re-migrates, and a downgrade to an older build
    // would then find a blob it cannot read. Exactly what ks_config_nvs.c does.
    if (r == CFG_DECODE_MIGRATED) {
        config_save(cfg);
        return;
    }

    // Defaults are already in *cfg (config_decode guarantees it). If there is no
    // blob but the old field-keys are there, this is the first boot after the
    // upgrade — migrate them once and write the blob back, so the next boot takes
    // the fast path above and the legacy keys are never read again.
    if (!have_blob) {
        prefs.begin(NS, true);
        bool migrated = migrate_legacy(cfg);
        prefs.end();
        if (migrated) config_save(cfg);
    }
}

extern "C" void config_save(const AppConfig* cfg) {
    prefs.begin(NS, false);  // read-write
    // Blob and version are written together, so a power cut cannot leave a version
    // stamp vouching for a blob that was never written (or vice versa).
    prefs.putBytes(KEY_CFG, cfg, sizeof(*cfg));
    prefs.putUInt(KEY_VER, APP_CONFIG_VERSION);
    prefs.end();
}
