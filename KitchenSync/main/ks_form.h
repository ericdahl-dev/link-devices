#pragma once
// Pure resolver for the KitchenSync config POST body (ARC-006). Owns the three fiddly,
// previously-untested steps that used to sit inside ks_web.cpp's save_handler:
//   1. URL-decode (%XX and '+') of each key and value,
//   2. splitting the "key=value&key=value" body,
//   3. "unchecked checkbox is absent -> off" reconciliation,
// then replays each pair through the tested ks_config_set grammar so the field
// rules live in exactly one place. No I/O, no NVS, no validation — the caller
// checks ks_config_valid on the result. Host-tested in test/test_ks_form.c.
#include "ks_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Resolve a FULL config form into *out, starting from *base. Mutates `body` in
// place (decode + tokenize). Checkbox-backed booleans (clock out, metronome,
// accent, per-output enables) are cleared before the walk, so a checkbox absent
// from the body reads as off; a present key turns its own back on. Value fields
// absent from the body keep their base value. Used by POST /save.
void ks_form_resolve(char *body, const KsConfig *base, KsConfig *out);

// Apply a PARTIAL body as a patch onto *base: set only the fields whose keys are
// present, leave everything else at its base value (no checkbox clearing). Mutates
// `body` in place. Used by POST /live, where a body carrying one field must not
// disable the checkboxes it happens to omit. ks_form_resolve is this plus a
// checkbox pre-clear.
void ks_form_apply(char *body, const KsConfig *base, KsConfig *out);

#ifdef __cplusplus
}
#endif
