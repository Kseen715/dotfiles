/* modules/amacc.c -- AMaCC, a JIT C compiler for ARM32 (jserv/amacc).
 *
 * AMaCC compiles a C subset and runs it in the same process; the driver
 * itself is an ARM32 ELF built by an arm-linux-gnueabihf cross gcc, so both
 * the compiler and its output are 32-bit ARM no matter what the host is.
 * Upstream's mk/arm.mk refuses to build without that cross gcc AND qemu-arm
 * on PATH, which is why `arm-cross` is in the package list.
 *
 * HOW IT RUNS -- an aarch64 kernel with CONFIG_COMPAT (a Raspberry Pi under
 * Ubuntu 24.04, for one) executes the ARM32 driver directly, provided the
 * armhf runtime libc is installed; everywhere else it runs under qemu-arm
 * with the cross toolchain's sysroot. The recipe tries the native path once
 * and writes whichever wrapper worked into $PREFIX/bin/amacc -- a symlink
 * would not do, since the qemu case needs the extra argv.
 *
 * NOT A BUILD COMPILER -- it cannot build os-rice: the C subset is far short
 * of what the tree uses, `-c` is not among its flags (usage is
 * `amacc [-s] [-o object] file`), and its ELF output, unlike its JIT, does
 * not survive here -- the emitted binary dies with a bus error natively and
 * trips an ld.so assertion under qemu-arm. Only the JIT mode is verified,
 * hence hosted = 0: the ccsrc hello world compiles a binary and runs it,
 * which is not a thing this compiler does.
 *
 * Idempotent (SS2), user-local (~/.local/share/amacc), no elevation. C89.
 */
#include "../lib/ccsrc.h"

#include <stddef.h>

int osrm_amacc(void) {
    static const char *const pkgs[] = { "build", "git", "arm-cross", NULL };
    static const OsrCcSource amacc = {
        "amacc",
        "https://github.com/jserv/amacc",
        pkgs,
        "make\n"
        "if ./amacc tests/hello.c >/dev/null 2>&1; then\n"
        "    printf '#!/bin/sh\\nexec \"%s/amacc\" \"$@\"\\n' \"$SRC\" > \"$PREFIX/bin/amacc\"\n"
        "else\n"
        "    SYSROOT=$(arm-linux-gnueabihf-gcc --print-sysroot)\n"
        "    [ -d \"$SYSROOT\" ] || SYSROOT=/usr/arm-linux-gnueabihf\n"
        "    printf '#!/bin/sh\\nexec qemu-arm -L \"%s\" \"%s/amacc\" \"$@\"\\n' \\\n"
        "        \"$SYSROOT\" \"$SRC\" > \"$PREFIX/bin/amacc\"\n"
        "fi\n"
        "chmod +x \"$PREFIX/bin/amacc\"\n"
        "\"$PREFIX/bin/amacc\" tests/hello.c\n",
        0
    };
    return osr_cc_from_source(&amacc);
}
