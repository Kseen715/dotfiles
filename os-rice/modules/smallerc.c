/* modules/smallerc.c -- SmallerC, a simple 16/32-bit C compiler
 * (alexfru/SmallerC).
 *
 * SmallerC targets 16- and 32-bit x86 with its own libc and cannot build
 * os-rice: it has no <dirent.h>, and its driver rejects -std=c89, -O2 and
 * -pedantic outright. What it does do is produce working 32-bit Linux
 * binaries (also DOS and Windows ones), which run on this box as-is.
 *
 * The prefix is compiled INTO the driver (common.mk turns $(prefix) into
 * -DPATH_PREFIX), so `make` and `make install` must be given the same
 * prefix; a tree built with the default /usr/local and installed elsewhere
 * reports `smlrpp: not found` at the first compile.
 *
 * The driver is smlrcc, so that -- not "smallerc" -- is the name on PATH and
 * the directory under ~/.local/share.
 *
 * Idempotent (SS2), user-local, no elevation. C89.
 */
#include "../lib/ccsrc.h"

#include <stddef.h>

int osrm_smallerc(void) {
    static const char *const pkgs[] = { "build", "git", NULL };
    static const OsrCcSource smlrcc = {
        "smlrcc",
        "https://github.com/alexfru/SmallerC",
        pkgs,
        "make prefix=\"$PREFIX\"\n"
        "make prefix=\"$PREFIX\" install\n",
        1
    };
    return osr_cc_from_source(&smlrcc);
}
