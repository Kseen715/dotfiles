/* modules/dkms.c -- DKMS + kernel headers for every installed kernel flavor. ONE
 * copy, POSIX (was .../modules/dkms.sh). Headers must match the *running* kernel,
 * so this is validated on hardware, not in CI (§9). Arch-only (kernel package
 * names are pacman's); no-op elsewhere.
 *
 * Port of modules/dkms.sh, kept as the reference at
 * test/ref/dkms_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <string.h>

int osrm_dkms(void) {
    static const char *const dkms[] = { "dkms", NULL };
    static const char *const kernels[] = { "linux", "linux-lts", "linux-zen", NULL };
    size_t i;
    int ok;

    if (strcmp(osr_mod_pkg(), "pacman") != 0) {
        osr_info("dkms module is Arch-specific - skipping");
        return 1;
    }
    ok = osr_pkg_install_step("Installing DKMS", dkms);
    /* Headers for the kernels this box actually has, not for all three: a
     * -headers package for a kernel that is not installed is a pointless
     * download and, on a rerun, a pointless upgrade. */
    for (i = 0; kernels[i] != NULL; i++) {
        char *q[4];
        q[0] = (char *)"pacman"; q[1] = (char *)"-Qq"; q[2] = (char *)kernels[i]; q[3] = NULL;
        if (osr_run_quiet(q) == 0) {
            Str desc, pkg;
            const char *one[2];
            str_init(&desc); str_init(&pkg);
            str_addz(&desc, "Installing "); str_addz(&desc, kernels[i]);
            str_addz(&desc, " headers");
            str_addz(&pkg, kernels[i]); str_addz(&pkg, "-headers");
            one[0] = str_text(&pkg); one[1] = NULL;
            ok = osr_pkg_install_step(str_text(&desc), one) && ok;
            str_free(&desc); str_free(&pkg);
        }
    }
    return ok;
}
