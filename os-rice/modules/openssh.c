/* modules/openssh.c -- OpenSSH client/server + sshd enabled. ONE copy, POSIX
 * (was .../modules/openssh.sh). Service control goes through enable_service so it
 * works on any init (§8), not just systemd's systemctl.
 *
 * Was modules/openssh.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_openssh(void) {
    static const char *const pkgs[] = { "openssh", NULL };
    int ok;

    ok = osr_pkg_install_step("Installing OpenSSH", pkgs);
    return osr_service_enable("sshd") && ok;
}
