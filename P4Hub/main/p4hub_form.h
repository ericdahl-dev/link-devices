#pragma once
// Pure resolver for the P4Hub config POST body (ARC-006). Owns the three fiddly,
// previously-untested steps that used to sit inside p4hub_web.cpp's save_handler:
//   1. URL-decode (%XX and '+') of each key and value,
//   2. splitting the "key=value&key=value" body,
//   3. "unchecked checkbox is absent -> off" reconciliation,
// then replays each pair through the tested p4hub_config_set grammar so the field
// rules live in exactly one place. No I/O, no NVS, no validation — the caller
// checks p4hub_config_valid on the result. Host-tested in test/test_p4hub_form.c.
#include "p4hub_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Resolve an application/x-www-form-urlencoded body into *out, starting from
// *base. Mutates `body` in place (decode + tokenize). Checkbox-backed booleans
// (clock out, metronome, accent, per-output enables) are cleared before the
// walk, so a checkbox absent from the body reads as off; a present key turns its
// own back on. Value fields absent from the body keep their base value.
void p4hub_form_resolve(char *body, const P4HubConfig *base, P4HubConfig *out);

#ifdef __cplusplus
}
#endif
