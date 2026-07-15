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
    /* ESP-030 pt3: exactly what the writer was hardcoding, so an existing device's
     * clock does not change when it migrates. 24 PPQN = MIDI clock; swing 0 = straight. */
    cfg->ppqn             = 24;
    cfg->swing_mbeats     = 0;
    /* ESP-037: 120.000 BPM, the Link default. Used when SOLO; a Link session overrides. */
    cfg->tempo_mbpm       = 120000;
}

static bool is_bool(int v) { return v == 0 || v == 1; }

bool config_validate(const AppConfig* cfg) {
    if (cfg->quantum_beats < 1 || cfg->quantum_beats > 64) return false;   // up to 16 bars
    if (!is_bool(cfg->clock_enable))     return false;
    if (!is_bool(cfg->transport_enable)) return false;
    if (!is_bool(cfg->play_on_release))  return false;
    if (cfg->nudge_mbeats < -250 || cfg->nudge_mbeats > 250) return false;  // +-1/4 beat
    if (cfg->brightness < 10 || cfg->brightness > 100) return false;        // never full-dark
    /* ESP-030 pt3: garbage in the form must never reach the clock writer. Same ranges
     * as the P4's ClockOutputCfg, so one client rule covers the fleet. */
    if (cfg->ppqn < 1 || cfg->ppqn > 48) return false;
    if (cfg->swing_mbeats < 0 || cfg->swing_mbeats > 250) return false;
    /* ESP-037: milli-BPM in the musical band. Literals, not master_clock's MIN/MAX --
     * this pure config layer must not pull in the Link-typed clock header. Mirrors
     * MASTER_CLOCK_BPM_MIN/MAX (20..300) * 1000; keep them in step. */
    if (cfg->tempo_mbpm < 20000 || cfg->tempo_mbpm > 300000) return false;
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
/* ESP-030 pt3: a v2 blob, upgraded. The bench unit holds one of these RIGHT NOW.
 *
 * Read through the FROZEN AppConfigV2 layout. v2 already had the WiFi slots, so this
 * is a straight field copy plus DEFAULTS for the two new ones -- and those defaults
 * are exactly what the writer was hardcoding (24 PPQN, no swing), so a migrated device
 * emits the same clock it did before. Nothing changes until the user asks for it. */
static cfg_decode_result migrate_v2(AppConfig* out, const void* blob, size_t blob_len) {
    if (blob_len != sizeof(AppConfigV2)) return CFG_DECODE_DEFAULTED;

    AppConfigV2 v2;
    memcpy(&v2, blob, sizeof(v2));

    AppConfig up;
    config_defaults(&up);            /* ppqn/swing start at the writer's old constants */

    for (int i = 0; i < KS_WIFI_SLOTS; i++) up.wifi[i] = v2.wifi[i];
    up.quantum_beats    = v2.quantum_beats;
    up.clock_enable     = v2.clock_enable;
    up.transport_enable = v2.transport_enable;
    up.play_on_release  = v2.play_on_release;
    up.nudge_mbeats     = v2.nudge_mbeats;
    up.brightness       = v2.brightness;

    if (!config_validate(&up)) return CFG_DECODE_DEFAULTED;

    *out = up;
    return CFG_DECODE_MIGRATED;
}

/* ESP-037: a v3 blob, upgraded. The bench Super Mini holds one of these. v3 had every
 * field but the tempo, so this is a straight copy plus the tempo DEFAULT (120 BPM) --
 * a migrated box free-runs at 120 exactly as it did before there was a tempo to set. */
static cfg_decode_result migrate_v3(AppConfig* out, const void* blob, size_t blob_len) {
    if (blob_len != sizeof(AppConfigV3)) return CFG_DECODE_DEFAULTED;

    AppConfigV3 v3;
    memcpy(&v3, blob, sizeof(v3));

    AppConfig up;
    config_defaults(&up);            /* tempo_mbpm starts at 120000 */

    for (int i = 0; i < KS_WIFI_SLOTS; i++) up.wifi[i] = v3.wifi[i];
    up.quantum_beats    = v3.quantum_beats;
    up.clock_enable     = v3.clock_enable;
    up.transport_enable = v3.transport_enable;
    up.play_on_release  = v3.play_on_release;
    up.nudge_mbeats     = v3.nudge_mbeats;
    up.brightness       = v3.brightness;
    up.ppqn             = v3.ppqn;
    up.swing_mbeats     = v3.swing_mbeats;

    if (!config_validate(&up)) return CFG_DECODE_DEFAULTED;

    *out = up;
    return CFG_DECODE_MIGRATED;
}

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
    if (version == 2u) return migrate_v2(out, blob, blob_len);
    if (version == 3u) return migrate_v3(out, blob, blob_len);

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
        case ACF_PPQN:             t.ppqn             = value; break;   /* ESP-030 pt3 */
        case ACF_SWING_MBEATS:     t.swing_mbeats     = value; break;   /* ESP-030 pt3 */
        case ACF_TEMPO_MBPM:       t.tempo_mbpm       = value; break;   /* ESP-037 */
        default: return false;
    }
    if (!config_validate(&t)) return false;   // keep the change only if it validates
    *cfg = t;
    return true;
}
