/* modules/zip.c -- zip + unzip archivers. ONE copy, POSIX (was .../modules/zip.sh).
 *
 * Port of modules/zip.sh, kept as the reference at
 * test/ref/zip_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_zip(void) {
    static const char *const pkgs[] = { "zip", "unzip", NULL };
    return osr_pkg_install_step("Installing zip and unzip", pkgs);
}
