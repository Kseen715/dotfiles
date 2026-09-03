/* modules/lacc.c -- lacc, a C89 compiler with an x86-64 backend (larmel/lacc).
 *
 * The one compiler in this round that builds the whole os-rice tree: it is
 * C89 by design, targets x86-64 SysV, writes ELF objects itself and links
 * against the host's glibc, so `CC=lacc ./build/nob -t` produces a working
 * build/osr (see the compiler table in README.md).
 *
 * WHY THE PATCH -- lacc's preprocessor requires the '(' of a function-like
 * macro invocation to be on the same line as the macro name. C89 3.8.3 does
 * not, and glibc's <signal.h> writes one across two lines, so an unpatched
 * lacc stops with
 *
 *   (/usr/include/signal.h, 369) error: Expected { but got
 *   __attribute_deprecated_msg__.
 *
 * modules/src/lacc-macro-newline.patch makes read_macro_invocation skip
 * newlines when it is not reading a directive. Upstream's own test suite is
 * unchanged by it (260 pass, the same 6 fail as without it).
 *
 * Idempotent (SS2), user-local (~/.local/share/lacc), no elevation. C89.
 */
#include "../lib/ccsrc.h"

#include <stddef.h>

int osrm_lacc(void) {
    static const char *const pkgs[] = { "build", "git", NULL };
    static const OsrCcSource lacc = {
        "lacc",
        "https://github.com/larmel/lacc",
        pkgs,
        "git apply \"$MODROOT/modules/src/lacc-macro-newline.patch\"\n"
        "./configure --prefix=\"$PREFIX\"\n"
        "make\n"
        "make install\n",
        1
    };
    return osr_cc_from_source(&lacc);
}
