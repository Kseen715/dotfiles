/* modules/tcc.c -- TinyCC, the Tiny C Compiler: a ~100KB C99 compiler that
 * builds a translation unit in the time gcc spends parsing its own flags, and
 * runs one straight from source with `tcc -run file.c`. It is the toolchain
 * this repo's own C core is quick-iterated against (nob.c picks up whatever
 * cc is on PATH), and the one worth having on a box too small for a full gcc.
 *
 * Native-first (§1a G6): every target ships tcc as a package, so there is no
 * source: fallback and no pkgmap row except where the real name differs --
 * Gentoo's atom (dev-lang/tcc) and Alpine, where the compiler package carries
 * neither the static libs it links against nor musl's headers, so `tcc hello.c`
 * fails at link time with the bare name alone (apk.map).
 *
 * Linux-only, so this file is all POSIX branch: there is no #ifdef _WIN32 half
 * the way modules/fastfetch.c has one. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_tcc(void) {
    static const char *const pkgs[] = { "tcc", NULL };
    return osr_pkg_install_step("Installing TinyCC", pkgs);
}
