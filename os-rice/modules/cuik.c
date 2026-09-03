/* modules/cuik.c -- Cuik, an alpha C compiler over its own x64 backend
 * (RealNeGate/Cuik).
 *
 * Alpha, and it shows: Cuik has no -std switch and rejects the warning
 * flags nob.c emits, and its preprocessor expands the predefined `linux`
 * macro inside a header name, so `#include <linux/limits.h>` becomes
 * `couldn't find file: 1/limits.h` and the os-rice build stops there.
 * Installed for the same reason as arocc -- it is a real compiler, just not
 * one that can build this tree yet.
 *
 * The build is unusual for a C compiler: build.lua drives it, and it needs
 * LuaJIT specifically (plain Lua 5.4 chokes on the script's `0x...u`
 * integer suffixes and on `unpack`), plus ninja and nasm, and clang with
 * lld because the generated link line passes -fuse-ld=lld.
 *
 * The binary reads its freestanding headers from the checkout, so
 * $PREFIX/bin/cuik is an exec wrapper, as in modules/xcc.c.
 *
 * Idempotent (SS2), user-local (~/.local/share/cuik), no elevation. C89.
 */
#include "../lib/ccsrc.h"

#include <stddef.h>

int osrm_cuik(void) {
    static const char *const pkgs[] = {
        "build", "git", "cuik-build-deps", NULL
    };
    static const OsrCcSource cuik = {
        "cuik",
        "https://github.com/RealNeGate/Cuik",
        pkgs,
        "luajit build.lua\n"
        "printf '#!/bin/sh\\nexec \"%s/bin/cuik\" \"$@\"\\n' \"$SRC\" > \"$PREFIX/bin/cuik\"\n"
        "chmod +x \"$PREFIX/bin/cuik\"\n",
        1
    };
    return osr_cc_from_source(&cuik);
}
