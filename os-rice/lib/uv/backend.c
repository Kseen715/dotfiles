/* lib/uv/backend.c -- the backend registry and the small shared pieces of
 * lib/uv/backend.h: domain names, capability defaults, and uv_detect's table.
 *
 * The table is ordered most-specific first, with generic_opp last because it
 * claims anything. Backends that only exist on one architecture are compiled
 * out entirely (nob.c decides), so this file's #ifdefs are the only place the
 * arch question is asked twice.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "backend.h"

static const char *const domain_names[UV_DOMAIN_MAX] = {
    "core", "cache", "gpu", "uncore"
};

const char *uv_domain_name(UvDomain d) {
    if (d < 0 || d >= UV_DOMAIN_MAX) return "?";
    return domain_names[d];
}

UvDomain uv_domain_parse(const char *name) {
    int i;
    if (name == NULL) return UV_DOMAIN_MAX;
    for (i = 0; i < UV_DOMAIN_MAX; i++) {
        if (strcmp(name, domain_names[i]) == 0) return (UvDomain)i;
    }
    return UV_DOMAIN_MAX;
}

void uv_caps_init(UvCaps *caps) {
    int i;
    memset(caps, 0, sizeof(*caps));
    caps->backend = "none";
    caps->detail[0] = '\0';
    for (i = 0; i < UV_DOMAIN_MAX; i++) {
        caps->present[i] = 0;
        caps->readable[i] = 0;
        caps->writable[i] = 0;
        caps->count[i] = 0;
    }
    /* Deliberately useless defaults: a backend that forgets to fill these in
     * gets a range that permits nothing, rather than one that permits -1000mV.
     * min_mv > max_mv is the "no range at all" spelling. */
    caps->min_mv = 0;
    caps->max_mv = 0;
    caps->step_mv = 0;
    caps->settings_volatile = 0;
    caps->needs_reboot = 0;
}

int uv_caps_any_writable(const UvCaps *caps) {
    int i;
    for (i = 0; i < UV_DOMAIN_MAX; i++) {
        if (caps->writable[i] && caps->count[i] > 0) return 1;
    }
    return 0;
}

/* backends -- most specific first. generic_opp is last and always claims. */
static const UvBackend *const backends[] = {
    &uv_backend_generic_opp
};
#define BACKEND_COUNT (sizeof(backends) / sizeof(backends[0]))

const UvBackend *uv_detect(UvCaps *caps, Str *report) {
    size_t i;
    for (i = 0; i < BACKEND_COUNT; i++) {
        uv_caps_init(caps);
        if (backends[i]->probe(caps, report)) return backends[i];
        /* A backend that passed must not have left half a report behind; it
         * is the probe's job to append nothing until it has decided. */
    }
    /* Unreachable while generic_opp is in the table, but a NULL return here
     * would be a crash in every caller, so fall back to it explicitly. */
    uv_caps_init(caps);
    return &uv_backend_generic_opp;
}
