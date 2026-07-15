#pragma once
// ESP-042: the ONE final migration that retires the Touch's AppConfig. It reads a legacy
// Touch config blob (versions 1..4, the frozen KTouchLegacy* layouts) and produces the
// equivalent shared KsConfig, so a Touch that upgrades to the converged firmware keeps its
// WiFi credentials and every setting instead of falling to the SoftAP portal.
//
// Pure and host-tested (test/test_ktouch_migrate.c). The NVS glue (app_config_nvs.cpp's
// successor) calls this when it finds an old `ver` in the "kstouch" namespace, then writes
// the result back as a KsConfig (KS_CONFIG_VERSION) so the migration runs exactly once.
//
// The Touch drives a SINGLE clock output on DIN, so its per-output fields (ppqn, nudge,
// swing) map onto KsConfig's clock[0]; clock[1..3] stay at defaults (KsCaps hides them on
// the wire). Everything else is a same-named field copy.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ks_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Migrate a legacy Touch blob into `out`. Returns true on a clean, validated migration;
// false if `app_version` is not 1..4, `blob_len` does not match that version's frozen
// size, or the migrated result fails ks_config_valid (bit-rot). On false, `out` is left
// as clean defaults so the caller can load those instead.
bool ktouch_migrate_legacy_config(KsConfig* out, const void* blob, size_t blob_len,
                                  uint32_t app_version);

#ifdef __cplusplus
}
#endif
