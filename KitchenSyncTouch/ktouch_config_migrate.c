#include "ktouch_config_migrate.h"
#include "ktouch_legacy_config.h"
#include <string.h>

// ESP-042: the four legacy Touch layouts share the same tail; only how far the tail
// reaches differs (v2 stopped before ppqn/swing, v3 before tempo). Rather than four
// near-identical copies, migrate onto a common shape and let the reader fill only what
// its version carried — the rest keeps the KsConfig default, exactly as the AppConfig
// vN->vN+1 chain did (ppqn 24, swing 0, tempo 120000).
//
// The Touch drives ONE clock output (DIN), so ppqn/nudge/swing land on clock[0]; the
// master clock gate becomes clock_out_enable. clock[0].enable stays 1 (a default), so a
// migrated box emits the same clock it did before.
static void apply_common(KsConfig* out, const WifiCred wifi[KS_WIFI_SLOTS],
                         int quantum_beats, int clock_enable, int transport_enable,
                         int play_on_release, int nudge_mbeats, int brightness) {
    for (int i = 0; i < KS_WIFI_SLOTS; i++) out->wifi[i] = wifi[i];
    out->quantum_beats        = quantum_beats;
    out->clock_out_enable     = clock_enable;
    out->transport_enable     = transport_enable;
    out->play_on_release      = play_on_release;
    out->lcd_brightness       = brightness;
    out->clock[0].phase_mbeats = nudge_mbeats;
}

bool ktouch_migrate_legacy_config(KsConfig* out, const void* blob, size_t blob_len,
                                  uint32_t app_version) {
    ks_config_defaults(out);   // on any reject the caller loads these; also seeds the
                               // fields a shorter (older) blob never carried
    if (!blob) return false;

    switch (app_version) {
        case 1u: {
            if (blob_len != sizeof(KTouchLegacyV1)) return false;
            KTouchLegacyV1 v1;
            memcpy(&v1, blob, sizeof(v1));
            // v1's single credential becomes slot 0; the newer slots stay EMPTY (from
            // defaults), never filled with bytes that followed a shorter blob. v1's ssid
            // field was 64 bytes; WifiCred::ssid is 33 — truncate and NUL-terminate (an
            // over-32 SSID was never valid 802.11 anyway).
            strncpy(out->wifi[0].ssid, v1.wifi_ssid, sizeof(out->wifi[0].ssid) - 1);
            out->wifi[0].ssid[sizeof(out->wifi[0].ssid) - 1] = '\0';
            strncpy(out->wifi[0].pass, v1.wifi_pass, sizeof(out->wifi[0].pass) - 1);
            out->wifi[0].pass[sizeof(out->wifi[0].pass) - 1] = '\0';
            out->quantum_beats     = v1.quantum_beats;
            out->clock_out_enable  = v1.clock_enable;
            out->transport_enable  = v1.transport_enable;
            out->play_on_release   = v1.play_on_release;
            out->lcd_brightness    = v1.brightness;
            out->clock[0].phase_mbeats = v1.nudge_mbeats;
            // ppqn/swing/tempo keep the defaults (24, 0, 120000).
            break;
        }
        case 2u: {
            if (blob_len != sizeof(KTouchLegacyV2)) return false;
            KTouchLegacyV2 v2;
            memcpy(&v2, blob, sizeof(v2));
            apply_common(out, v2.wifi, v2.quantum_beats, v2.clock_enable,
                         v2.transport_enable, v2.play_on_release, v2.nudge_mbeats,
                         v2.brightness);
            // ppqn/swing/tempo keep the defaults.
            break;
        }
        case 3u: {
            if (blob_len != sizeof(KTouchLegacyV3)) return false;
            KTouchLegacyV3 v3;
            memcpy(&v3, blob, sizeof(v3));
            apply_common(out, v3.wifi, v3.quantum_beats, v3.clock_enable,
                         v3.transport_enable, v3.play_on_release, v3.nudge_mbeats,
                         v3.brightness);
            out->clock[0].ppqn        = v3.ppqn;
            out->clock[0].swing_mbeats = v3.swing_mbeats;
            // tempo keeps the default (120000).
            break;
        }
        case 4u: {
            if (blob_len != sizeof(KTouchLegacyV4)) return false;
            KTouchLegacyV4 v4;
            memcpy(&v4, blob, sizeof(v4));
            apply_common(out, v4.wifi, v4.quantum_beats, v4.clock_enable,
                         v4.transport_enable, v4.play_on_release, v4.nudge_mbeats,
                         v4.brightness);
            out->clock[0].ppqn        = v4.ppqn;
            out->clock[0].swing_mbeats = v4.swing_mbeats;
            out->tempo_mbpm           = v4.tempo_mbpm;
            break;
        }
        default:
            return false;   // not a legacy Touch version
    }

    // A migration is not a licence to skip the guard — bit-rot is bit-rot at any version.
    // Same gate a verbatim KsConfig load runs through. On failure, restore clean defaults
    // so the caller never sees a half-migrated struct.
    if (!ks_config_valid(out)) {
        ks_config_defaults(out);
        return false;
    }
    return true;
}
