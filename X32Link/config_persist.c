#include "config_persist.h"

void config_persist_reset(ConfigPersist* p) {
    if (!p) return;
    p->first_edit_ms = 0;
    p->last_edit_ms  = 0;
    p->dirty         = false;
}

void config_persist_mark(ConfigPersist* p, uint32_t now_ms) {
    if (!p) return;
    if (!p->dirty) {                  // first edit of a new burst starts the deferral clock
        p->first_edit_ms = now_ms;
        p->dirty         = true;
    }
    p->last_edit_ms = now_ms;
}

bool config_persist_due(ConfigPersist* p, uint32_t now_ms) {
    if (!p || !p->dirty) return false;

    // Unsigned subtraction, deliberately: it stays correct across the millis()
    // rollover at 2^32 ms (~49 days). Comparing timestamps directly would not.
    uint32_t quiet = now_ms - p->last_edit_ms;    // idle since the last edit
    uint32_t held  = now_ms - p->first_edit_ms;   // dirty since the burst began

    if (quiet >= CONFIG_PERSIST_QUIET_MS || held >= CONFIG_PERSIST_MAX_DEFER_MS) {
        p->dirty = false;
        return true;
    }
    return false;
}
