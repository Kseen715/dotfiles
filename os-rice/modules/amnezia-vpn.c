/* modules/amnezia-vpn.c -- AmneziaVPN client. ONE copy, POSIX
 * (was .../apps/amnezia-vpn-client.sh). Maps amneziavpn -> aur:amneziavpn-bin on
 * Arch; on apt -> source:provide_amneziavpn (upstream QtIFW installer, x86_64).
 *
 * Was modules/amnezia-vpn.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_amnezia_vpn(void) {
    static const char *const pkgs[] = { "amneziavpn", NULL };
    return osr_pkg_install_step("Installing AmneziaVPN", pkgs);
}
