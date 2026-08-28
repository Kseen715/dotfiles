/* modules/qpwgraph.c -- qpwgraph PipeWire patchbay. ONE copy, POSIX
 * (was .../modules/qpwgraph.sh). Native, no config.
 *
 * Port of modules/qpwgraph.sh, kept as the reference at
 * test/ref/qpwgraph_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_qpwgraph(void) {
    static const char *const pkgs[] = { "qpwgraph", NULL };
    return osr_pkg_install_step("Installing qpwgraph", pkgs);
}
