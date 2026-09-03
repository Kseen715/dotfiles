/* modules/cpu-microcodes.c -- CPU microcode package for the detected vendor. ONE
 * copy, POSIX (was .../modules/cpu-microcodes.sh). Uses OSR_CPU_VENDOR from
 * detect.sh. Hardware-dependent: correct only on the machine it runs on (§9).
 *
 * Two things the package alone does not do, both learned on a 2010 Westmere
 * laptop that was booting `microcode: Current revision: 0x00000002` and
 * `MDS: Vulnerable: Clear CPU buffers attempted, no microcode` with an intact
 * intel-ucode in the repos:
 *
 *   - On Void the blob is nonfree and the repo is opt-in, so the install was a
 *     no-op (lib/pkgmap/xbps.map used to map the name to nothing). The package
 *     layer's osr_pkg_nonfree() turns that repo on, announced and declinable
 *     with OSR_NONFREE=0 - it is not a microcode decision, and unrar and the
 *     nonfree firmware rows want the same switch.
 *   - Microcode is loaded by the CPU before the root filesystem exists, so it
 *     only ever takes effect from the INITRAMFS. Installing the package writes
 *     /usr/lib/firmware and nothing else; without a rebuild the update lands on
 *     no boot at all. Late loading through /sys is not the fallback: current
 *     kernels ship it off, and where it is on it cannot fix errata already
 *     latched at boot.
 *
 * Was modules/cpu-microcodes.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <string.h>

int osrm_cpu_microcodes(void) {
    static const char *const intel[] = { "intel-ucode", NULL };
    static const char *const amd[]   = { "amd-ucode", NULL };
    const char *vendor = env_str("OSR_CPU_VENDOR", "");
    int ok;

    if (strcmp(vendor, "GenuineIntel") == 0) {
        /* No repo, no package: installing the name would only produce a
         * "package not found" whose real cause is two steps back. Enabling the
         * repo is not this module's business either - Void keeps more than
         * microcode behind it - so the package layer owns the decision and the
         * OSR_NONFREE knob that declines it. */
        if (!osr_pkg_nonfree("Intel microcode")) return 1;
        ok = osr_pkg_install_step("Installing Intel microcode", intel);
    } else if (strcmp(vendor, "AuthenticAMD") == 0) {
        ok = osr_pkg_install_step("Installing AMD microcode", amd);
    } else {
        osr_warnf("unknown CPU vendor '%s' - no microcode installed", vendor);
        return 1;
    }

    /* Only the rebuild puts the blob where the CPU reads it. */
    ok = osr_initramfs_regen() && ok;
    osr_info("microcode is loaded at boot - reboot for it to take effect");
    return ok;
}
