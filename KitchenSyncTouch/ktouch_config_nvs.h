#pragma once
// ESP-042: NVS persistence for the Touch, now storing the SHARED KsConfig instead of the
// retired AppConfig. Namespace stays "kstouch" so a board reflashed from X32Link never
// reads its mixer keys into this box; the version key disambiguates a native KsConfig
// blob (KS_CONFIG_VERSION, >= 5) from a legacy AppConfig blob (1..4), which is migrated
// once via ktouch_migrate_legacy_config and written back.
#include "ks_config.h"

#ifdef __cplusplus
extern "C" {
#endif

void config_load(KsConfig* cfg);
void config_save(const KsConfig* cfg);

#ifdef __cplusplus
}
#endif
