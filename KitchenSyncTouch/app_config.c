#include "app_config.h"
#include "config.h"
#include <string.h>

void config_defaults(AppConfig* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->quantum_beats    = DEFAULT_QUANTUM_BEATS;
    cfg->clock_enable     = DEFAULT_CLOCK_ENABLE;
    cfg->transport_enable = DEFAULT_TRANSPORT_ENABLE;
    cfg->play_on_release  = DEFAULT_PLAY_ON_RELEASE;
    cfg->nudge_mbeats     = DEFAULT_NUDGE_MBEATS;
    cfg->brightness       = DEFAULT_BRIGHTNESS;
}

static bool is_bool(int v) { return v == 0 || v == 1; }

bool config_validate(const AppConfig* cfg) {
    if (cfg->quantum_beats < 1 || cfg->quantum_beats > 64) return false;   // up to 16 bars
    if (!is_bool(cfg->clock_enable))     return false;
    if (!is_bool(cfg->transport_enable)) return false;
    if (!is_bool(cfg->play_on_release))  return false;
    if (cfg->nudge_mbeats < -250 || cfg->nudge_mbeats > 250) return false;  // +-1/4 beat
    if (cfg->brightness < 10 || cfg->brightness > 100) return false;        // never full-dark
    return true;   // empty ssid is valid (AP setup mode); caller checks it separately
}

/* ESP-030: a v1 blob, UPGRADED rather than thrown away.
 *
 * Read through the FROZEN AppConfigV1 layout, never through the current struct --
 * v1 bytes must be read at v1 offsets, or a device gets handed garbage where its
 * password used to be.
 *
 * The single credential becomes wifi[0]; the new slots come up EMPTY, never filled
 * with whatever bytes happened to follow a shorter blob. Everything else carries
 * across verbatim -- a migration that keeps the network but silently forgets the
 * user's nudge has still lost their config. */
static cfg_decode_result migrate_v1(AppConfig* out, const void* blob, size_t blob_len) {
    if (blob_len != sizeof(AppConfigV1)) return CFG_DECODE_DEFAULTED;

    AppConfigV1 v1;
    memcpy(&v1, blob, sizeof(v1));

    AppConfig up;
    config_defaults(&up);            /* the new slots start empty, by construction */

    /* v1's ssid field was 64 bytes; WifiCred::ssid is 33. Truncate deliberately and
     * always NUL-terminate. An over-long SSID was never valid anyway (802.11 caps at
     * 32), and running off the end of the destination is not the alternative. */
    strncpy(up.wifi[0].ssid, v1.wifi_ssid, sizeof(up.wifi[0].ssid) - 1);
    up.wifi[0].ssid[sizeof(up.wifi[0].ssid) - 1] = '\0';
    strncpy(up.wifi[0].pass, v1.wifi_pass, sizeof(up.wifi[0].pass) - 1);
    up.wifi[0].pass[sizeof(up.wifi[0].pass) - 1] = '\0';

    up.quantum_beats    = v1.quantum_beats;
    up.clock_enable     = v1.clock_enable;
    up.transport_enable = v1.transport_enable;
    up.play_on_release  = v1.play_on_release;
    up.nudge_mbeats     = v1.nudge_mbeats;
    up.brightness       = v1.brightness;

    /* The v1 bytes still have to be in range -- bit-rot is bit-rot at any version. */
    if (!config_validate(&up)) return CFG_DECODE_DEFAULTED;

    *out = up;
    return CFG_DECODE_MIGRATED;
}

// ARC-020: the single owner of "is this persisted blob safe to load?" — see
// app_config.h. Every gate is fail-closed: anything we cannot positively vouch
// for becomes defaults, because loading a stale layout puts garbage into fields
// the user never sees until the clock misfires or the screen goes black.
cfg_decode_result config_decode(AppConfig* out, const void* blob, size_t blob_len,
                                bool version_present, uint32_t version) {
    config_defaults(out);

    // Bytes with no version key predate versioning: they could be any old
    // layout, so no size coincidence earns them a load.
    if (!blob || !version_present) return CFG_DECODE_DEFAULTED;

    // ESP-030: an OLD but READABLE blob is UPGRADED, never discarded. Defaulting
    // here would make every Touch in the field lose its WiFi credentials on the
    // next boot, fall back to SoftAP, and drop off the network. The P4 learned
    // this the hard way in ESP-013.
    if (version == 1u) return migrate_v1(out, blob, blob_len);

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
    AppConfig t = *cfg;
    switch (field) {
        case ACF_QUANTUM_BEATS:    t.quantum_beats    = value; break;
        case ACF_CLOCK_ENABLE:     t.clock_enable     = value; break;
        case ACF_TRANSPORT_ENABLE: t.transport_enable = value; break;
        case ACF_PLAY_ON_RELEASE:  t.play_on_release  = value; break;
        case ACF_NUDGE_MBEATS:     t.nudge_mbeats     = value; break;
        case ACF_BRIGHTNESS:       t.brightness       = value; break;
        default: return false;
    }
    if (!config_validate(&t)) return false;   // keep the change only if it validates
    *cfg = t;
    return true;
}
