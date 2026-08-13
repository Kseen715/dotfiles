/* provide_module.c -- see provide_module.h. The metapacket: every builder's
 * source file is included here, so the whole set is one translation unit and
 * adding a recipe costs no build plumbing. Same relationship lib/build.sh has
 * to the shell functions it defines -- one file that puts them all in scope.
 *
 * C89.
 */
#include "provide_module.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "lib/ui.h"
#include "lib/winbin.h"
#include "lib/winpkg.h"

/* --- helpers shared by every builder -------------------------------------
 * Defined before the metapacket block below, so each included builder can
 * use them without a header of its own -- the same convenience lib/build.sh
 * gets from _osr_install_tarball_bin and friends sitting above the
 * provide_* functions in one file.
 * ---------------------------------------------------------------------- */

/* osrp_join -- build a path or command line from pieces, NULL-terminated.
 * Returns 0 (leaving dst empty) if the result would not fit, because a
 * truncated path is a command that acts on the wrong file. Builders use
 * this instead of sprintf: the pieces are runtime paths whose length the
 * compiler cannot check.
 */
static int osrp_join(char *dst, unsigned long dst_sz, const char *first, ...) {
    va_list ap;
    const char *piece;
    unsigned long len;

    dst[0] = '\0';
    len = 0;

    va_start(ap, first);
    for (piece = first; piece != NULL; piece = va_arg(ap, const char *)) {
        unsigned long piece_len = (unsigned long)strlen(piece);
        if (len + piece_len >= dst_sz) {
            va_end(ap);
            dst[0] = '\0';
            return 0;
        }
        memcpy(dst + len, piece, piece_len + 1);
        len += piece_len;
    }
    va_end(ap);

    return 1;
}

/* --- the metapacket ------------------------------------------------------
 * One #include per package. Each file defines exactly one
 * `static int provide_<name>(const char *map_path, const char *name,
 *                            const char *test_command)`.
 * ---------------------------------------------------------------------- */

#include "provide/wezterm.c"

/* --- registry ------------------------------------------------------------
 * The name here is what a windows.map row writes after `source:`. Keeping
 * it an explicit table rather than deriving it from the function name means
 * a typo in the map is caught as "unknown builder" instead of resolving to
 * nothing.
 * ---------------------------------------------------------------------- */

static const struct {
    const char *name;
    osr_provide_fn fn;
    int needs_admin;
} osr_providers[] = {
    { "provide_wezterm", provide_wezterm, 0 }
};

#define OSR_PROVIDER_COUNT (sizeof(osr_providers) / sizeof(osr_providers[0]))

static int provider_index(const char *fn_name) {
    unsigned long i;
    for (i = 0; i < OSR_PROVIDER_COUNT; i++) {
        if (strcmp(osr_providers[i].name, fn_name) == 0) return (int)i;
    }
    return -1;
}

int osr_provide_known(const char *fn_name) {
    return provider_index(fn_name) >= 0;
}

int osr_provide_needs_admin(const char *fn_name) {
    int i = provider_index(fn_name);
    return (i >= 0) ? osr_providers[i].needs_admin : 0;
}

int osr_provide_run(const char *fn_name, const char *map_path, const char *name,
                    const char *test_command) {
    int i;

    /* Idempotency lives here, not in the builders -- _via_source's
     * `command -v <name>` probe, in C. A builder is only ever entered when
     * there is genuinely work to do, so none of them need their own
     * already-installed check. */
    if (osr_winpkg_have_command(test_command)) {
        osr_info("%s already present (source) -- skipping", name);
        return 1;
    }

    i = provider_index(fn_name);
    if (i < 0) {
        /* The map named a builder that does not exist. That is a map error,
         * and saying so beats silently installing nothing. */
        osr_warn("source builder '%s' is not defined for %s -- add it to "
                 "provide/ and provide_module.c's registry", fn_name, name);
        return 0;
    }

    osr_info("building %s from source (%s)", name, fn_name);

    if (!osr_providers[i].fn(map_path, name, test_command)) {
        osr_warn("source build failed for %s", name);
        return 0;
    }

    return 1;
}
