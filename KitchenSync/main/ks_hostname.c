// Pure mDNS hostname derivation — see ks_hostname.h (ESP-012).
#include "ks_hostname.h"

static const char PREFIX[] = "kitchensync-";

void ks_hostname(const uint8_t mac[6], char* out, size_t cap) {
    if (!out || cap == 0) return;

    static const char HEX[] = "0123456789abcdef";
    // Build into a full-size local, then copy what fits. Writing straight into a
    // short `cap` would leave a half-formed name if it ran out mid-byte.
    char full[KS_HOSTNAME_MAX];
    size_t n = 0;
    for (const char* p = PREFIX; *p; p++) full[n++] = *p;
    full[n++] = HEX[(mac[4] >> 4) & 0xF];
    full[n++] = HEX[ mac[4]       & 0xF];
    full[n++] = HEX[(mac[5] >> 4) & 0xF];
    full[n++] = HEX[ mac[5]       & 0xF];
    full[n]   = '\0';

    size_t copy = (n < cap - 1) ? n : cap - 1;
    for (size_t i = 0; i < copy; i++) out[i] = full[i];
    out[copy] = '\0';
}
