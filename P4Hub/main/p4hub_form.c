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

void p4hub_form_resolve(char *body, const P4HubConfig *base, P4HubConfig *out) {
    *out = *base;
    // Checkboxes: an unchecked box is simply absent from the POST body, so its
    // absence means "off". Clear them up front; a present key flips its own back
    // on when replayed through p4hub_config_set. This replaces the save_handler
    // saw_* tracking and the clk<N>_en detection that duplicated the grammar.
    out->clock_out_enable = 0;
    out->metronome_enable = 0;
    out->metronome_accent = 0;
    for (int i = 0; i < P4HUB_CLOCK_OUTPUTS; i++) out->clock[i].enable = 0;

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
