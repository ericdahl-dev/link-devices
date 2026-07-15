#pragma once
// ESP-042: the Touch's OLD config layouts, FROZEN, kept ONLY so the final
// AppConfig->KsConfig migration can read blobs already in the field. When the Touch
// stored its own `AppConfig` (app_config.h, now deleted) it wrote these bytes; a device
// that upgrades to the converged firmware holds one of them in NVS and must not lose its
// WiFi credentials or settings. This header is READ-ONLY history: nothing here may ever
// change, exactly like the frozen structs inside app_config.h were.
//
// Byte-for-byte copies of what shipped:
//   v1  ESP-016  single wifi_ssid/wifi_pass, no ppqn/swing/tempo   (152 bytes)
//   v2  ESP-030  wifi[KS_WIFI_SLOTS]                               (316 bytes)
//   v3  ESP-030 pt3  + ppqn + swing_mbeats                         (324 bytes)
//   v4  ESP-037  + tempo_mbpm  (the LAST AppConfig layout)         (328 bytes)
#include <stddef.h>
#include "ks_config.h"   // the SHARED WifiCred / KS_WIFI_SLOTS

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
#  define KTOUCH_LEGACY_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#  define KTOUCH_LEGACY_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

typedef struct {
    char wifi_ssid[64];
    char wifi_pass[64];
    int  quantum_beats;
    int  clock_enable;
    int  transport_enable;
    int  play_on_release;
    int  nudge_mbeats;
    int  brightness;
} KTouchLegacyV1;
KTOUCH_LEGACY_STATIC_ASSERT(sizeof(KTouchLegacyV1) == 152,
    "the v1 Touch layout is FROZEN — it describes bytes already in the field");

typedef struct {
    WifiCred wifi[KS_WIFI_SLOTS];
    int  quantum_beats;
    int  clock_enable;
    int  transport_enable;
    int  play_on_release;
    int  nudge_mbeats;
    int  brightness;
} KTouchLegacyV2;
KTOUCH_LEGACY_STATIC_ASSERT(sizeof(KTouchLegacyV2) == 316,
    "the v2 Touch layout is FROZEN");

typedef struct {
    WifiCred wifi[KS_WIFI_SLOTS];
    int  quantum_beats;
    int  clock_enable;
    int  transport_enable;
    int  play_on_release;
    int  nudge_mbeats;
    int  brightness;
    int  ppqn;
    int  swing_mbeats;
} KTouchLegacyV3;
KTOUCH_LEGACY_STATIC_ASSERT(sizeof(KTouchLegacyV3) == 324,
    "the v3 Touch layout is FROZEN");

typedef struct {
    WifiCred wifi[KS_WIFI_SLOTS];
    int  quantum_beats;
    int  clock_enable;
    int  transport_enable;
    int  play_on_release;
    int  nudge_mbeats;
    int  brightness;
    int  ppqn;
    int  swing_mbeats;
    int  tempo_mbpm;
} KTouchLegacyV4;
KTOUCH_LEGACY_STATIC_ASSERT(sizeof(KTouchLegacyV4) == 328,
    "the v4 Touch layout is FROZEN — the LAST AppConfig, the bench unit holds one");

#ifdef __cplusplus
}
#endif
