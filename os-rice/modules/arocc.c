/* modules/arocc.c -- arocc, a C compiler written in Zig (Vexu/arocc).
 *
 * A front end with an incomplete backend: it parses os-rice fine and then
 * dies in code generation -- even a hello world ends at `fatal error: TODO
 * CodeGen.genVar` -- so it cannot build the tree. Installed for the front
 * end, which is worth having as a second opinion on a diagnostic.
 *
 * Built with `zig build`, and the version bar is high: arocc tracks Zig
 * master and needs 0.17.0-dev or newer, which is ahead of every packaged
 * Zig at the time of writing. On a box with an older Zig the build stops
 * with Zig's own version error and this module reports the failure -- run
 * `osr module zig` for a newer one, or install a master build by hand.
 *
 * Idempotent (SS2), user-local (~/.local/share/arocc), no elevation. C89.
 */
#include "../lib/ccsrc.h"

#include <stddef.h>

int osrm_arocc(void) {
    static const char *const pkgs[] = { "build", "git", "zig", NULL };
    static const OsrCcSource arocc = {
        "arocc",
        "https://github.com/Vexu/arocc",
        pkgs,
        "zig build -Doptimize=ReleaseFast --prefix \"$PREFIX\"\n",
        1
    };
    return osr_cc_from_source(&arocc);
}
