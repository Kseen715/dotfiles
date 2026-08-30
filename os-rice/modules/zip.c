/* modules/zip.c -- zip + unzip archivers. ONE copy, POSIX (was .../modules/zip.sh).
 *
 * Was modules/zip.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_zip(void) {
    static const char *const pkgs[] = { "zip", "unzip", NULL };
    return osr_pkg_install_step("Installing zip and unzip", pkgs);
}
