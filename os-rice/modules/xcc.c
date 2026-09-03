/* modules/xcc.c -- xcc, a self-hosting C compiler for x86-64/aarch64/riscv64
 * and WebAssembly (tyfkda/xcc).
 *
 * xcc ships its OWN libc rather than using the host's headers, which is why
 * it cannot build os-rice: the tree includes <dirent.h> and xcc answers
 * `Cannot open file: <dirent.h>`. It is installed anyway because it is a
 * complete, working compiler for programs that stay inside what its libc
 * covers.
 *
 * The driver finds cc1/cpp/as/ld and its include/ and lib/ directories
 * relative to argv[0], so the checkout IS the installation and $PREFIX/bin/
 * xcc is a two-line exec wrapper -- a symlink would make the driver look for
 * its parts in ~/.local/bin.
 *
 * Idempotent (SS2), user-local (~/.local/share/xcc), no elevation. C89.
 */
#include "../lib/ccsrc.h"

#include <stddef.h>

int osrm_xcc(void) {
    static const char *const pkgs[] = { "build", "git", NULL };
    static const OsrCcSource xcc = {
        "xcc",
        "https://github.com/tyfkda/xcc",
        pkgs,
        "make\n"
        "printf '#!/bin/sh\\nexec \"%s/xcc\" \"$@\"\\n' \"$SRC\" > \"$PREFIX/bin/xcc\"\n"
        "chmod +x \"$PREFIX/bin/xcc\"\n",
        1
    };
    return osr_cc_from_source(&xcc);
}
