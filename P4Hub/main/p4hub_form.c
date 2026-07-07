#include "p4hub_form.h"
#include <string.h>

static int hexval(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

// In-place URL-decode (%XX and '+') of one form field. Run per-token, after the
// body is split on '&', so an encoded %26 decodes to '&' without acting as a
// separator.
static void url_decode(char *s) {
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') {
            *o++ = ' ';
        } else if (*p == '%' && p[1] && p[2]) {
            *o++ = (char)(hexval(p[1]) * 16 + hexval(p[2]));
            p += 2;
        } else {
            *o++ = *p;
        }
    }
    *o = '\0';
}

// Walk "key=value&..." pairs, URL-decoding both sides, and apply each present key
// to *out via the tested grammar. `out` is already initialized by the caller.
static void apply_pairs(char *body, P4HubConfig *out) {
    char *save = NULL;
    for (char *pair = strtok_r(body, "&", &save); pair; pair = strtok_r(NULL, "&", &save)) {
        char *eq = strchr(pair, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = pair, *val = eq + 1;
        url_decode(key);
        url_decode(val);
        p4hub_config_set(out, key, val);   // per-field range failures ignored; caller validates
    }
}

void p4hub_form_apply(char *body, const P4HubConfig *base, P4HubConfig *out) {
    *out = *base;                 // patch: only keys present in the body change
    apply_pairs(body, out);
}

void p4hub_form_resolve(char *body, const P4HubConfig *base, P4HubConfig *out) {
    *out = *base;
    // Full form: an unchecked checkbox is simply absent from the POST body, so its
    // absence means "off". Clear the checkbox-backed booleans up front; a present
    // key flips its own back on. This is p4hub_form_apply plus the pre-clear, and
    // replaces the save_handler saw_* tracking + the duplicated clk<N>_en detection.
    out->clock_out_enable = 0;
    out->metronome_enable = 0;
    out->metronome_accent = 0;
    out->led_enable       = 0;
    for (int i = 0; i < P4HUB_CLOCK_OUTPUTS; i++) out->clock[i].enable = 0;

    apply_pairs(body, out);
}
