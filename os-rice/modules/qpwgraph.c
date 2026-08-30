/* modules/qpwgraph.c -- qpwgraph PipeWire patchbay. ONE copy, POSIX
 * (was .../modules/qpwgraph.sh). Native, no config.
 *
 * Was modules/qpwgraph.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_qpwgraph(void) {
    static const char *const pkgs[] = { "qpwgraph", NULL };
    return osr_pkg_install_step("Installing qpwgraph", pkgs);
}
