#pragma once
// ESP-012: the device's mDNS hostname, derived per unit so two KitchenSyncs on one
// LAN never collide. Pure — no ESP-IDF dependency, host-tested in
// test/test_ks_hostname.c. The glue (wifi_link.c) reads the MAC and advertises it.
//
// The plain name `kitchensync` is advertised separately as a delegated alias, so
// a single board on a network answers to both. Two boards answer to their own
// -XXXX names and fight over the alias; that is the documented cost of having a
// memorable name at all, and the per-unit name is always unambiguous.
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// "kitchensync-" (12) + 4 hex + NUL.
#define KS_HOSTNAME_MAX 17

// Write "kitchensync-<last two MAC bytes, lowercase hex>" into `out`.
// Always NUL-terminates. A `cap` smaller than the full name truncates rather
// than overflowing — the result is a visibly short name, not corruption.
void ks_hostname(const uint8_t mac[6], char* out, size_t cap);

#ifdef __cplusplus
}
#endif
