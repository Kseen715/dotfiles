/* modules/cpu-microcodes.c -- CPU microcode package for the detected vendor. ONE
 * copy, POSIX (was .../modules/cpu-microcodes.sh). Uses OSR_CPU_VENDOR from
 * detect.sh. Hardware-dependent: correct only on the machine it runs on (§9).
 *
 * Port of modules/cpu-microcodes.sh, kept as the reference at
 * test/ref/cpu-microcodes_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <string.h>

int osrm_cpu_microcodes(void) {
    static const char *const intel[] = { "intel-ucode", NULL };
    static const char *const amd[]   = { "amd-ucode", NULL };
    const char *vendor = env_str("OSR_CPU_VENDOR", "");

    if (strcmp(vendor, "GenuineIntel") == 0)
        return osr_pkg_install_step("Installing Intel microcode", intel);
    if (strcmp(vendor, "AuthenticAMD") == 0)
        return osr_pkg_install_step("Installing AMD microcode", amd);
    osr_warnf("unknown CPU vendor '%s' - no microcode installed", vendor);
    return 1;
}
