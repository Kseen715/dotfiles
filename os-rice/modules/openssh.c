/* modules/openssh.c -- OpenSSH client/server + sshd enabled. ONE copy, POSIX
 * (was .../modules/openssh.sh). Service control goes through enable_service so it
 * works on any init (§8), not just systemd's systemctl.
 *
 * Port of modules/openssh.sh, kept as the reference at
 * test/ref/openssh_sh_ref.sh. C89.
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
