/* modules/shecc.c -- shecc, a self-hosting C compiler for ARMv7-A and RV32IM
 * (sysprog21/shecc).
 *
 * A cross compiler on this box: shecc has no x86-64 backend, so it cannot
 * build os-rice and its output cannot run here -- the hello-world check the
 * other compiler modules do is skipped for exactly that reason.
 *
 * Only the stage-0 compiler (built by the host cc) is installed. Upstream's
 * `make` also wants to build the stage-1 and stage-2 self-hosted compilers
 * and needs qemu-arm to run them; without it, it prints
 *
 *   Warning: failed to build the stage 1 and stage 2 compilers due to
 *   missing qemu-arm
 *
 * and fails the whole `make`. The recipe therefore asks for `make config`
 * and the stage-0 target by name, and copies out/shecc itself -- upstream
 * has no install target.
 *
 * Idempotent (SS2), user-local (~/.local/share/shecc), no elevation. C89.
 */
#include "../lib/ccsrc.h"

#include <stddef.h>

int osrm_shecc(void) {
    static const char *const pkgs[] = { "build", "git", NULL };
    static const OsrCcSource shecc = {
        "shecc",
        "https://github.com/sysprog21/shecc",
        pkgs,
        "make config\n"
        "make out/shecc\n"
        "cp out/shecc \"$PREFIX/bin/shecc\"\n",
        0
    };
    return osr_cc_from_source(&shecc);
}
