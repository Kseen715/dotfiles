/* modules/networkmanager.c -- NetworkManager + enabled service. ONE copy, POSIX
 * (was .../modules/networkmanager.sh). lib32-libnm is Arch multilib (needs the
 * pacman-multilib module earlier); every other manager skips it via any.map, so
 * the 64-bit stack is what actually installs off Arch.
 * nm-applet is not cosmetic: it is the only thing that shows a wifi/VPN password
 * prompt under a bare WM. nmtui/nmcli ship inside the NetworkManager package.
 *
 * Was modules/networkmanager.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_networkmanager(void) {
    static const char *const pkgs[] = {
        "networkmanager", "network-manager-applet", "libnm", "lib32-libnm", NULL
    };
    int ok;

    ok = osr_pkg_install_step("Installing NetworkManager", pkgs);
    return osr_service_enable("NetworkManager") && ok;
}
