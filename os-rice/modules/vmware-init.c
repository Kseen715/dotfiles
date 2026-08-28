/* modules/vmware-init.c -- VMware guest tools + driver, only under a VMware
 * hypervisor. POSIX port of .../modules/vmware-init.sh, keyed on OSR_VIRT from
 * detect.sh. Hardware/VM-dependent (§9). Arch-only.
 *
 * Port of modules/vmware-init.sh, kept as the reference at
 * test/ref/vmware-init_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <string.h>

int osrm_vmware_init(void) {
    static const char *const tools[] = { "open-vm-tools", "mesa", NULL };
    static const char *const xorg[]  = { "xf86-video-vmware", NULL };
    int ok;

    if (strcmp(osr_mod_pkg(), "pacman") != 0) return 1;
    if (strcmp(env_str("OSR_VIRT", "none"), "vmware") != 0) {
        osr_infof("not a VMware guest (virt=%s) - skipping", env_str("OSR_VIRT", "none"));
        return 1;
    }
    ok = osr_pkg_install_step("Installing VMware guest tools", tools);
    ok = osr_pkg_install_step("Installing VMware Xorg driver (AUR)", xorg) && ok;
    ok = osr_service_enable("vmtoolsd") && ok;
    return osr_service_enable("vmware-vmblock-fuse") && ok;
}
