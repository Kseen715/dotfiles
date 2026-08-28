/* modules/printer.c -- CUPS + Samba/SMB client + Canon captdriver (AUR). POSIX
 * port of .../modules/printer.sh. Services enabled via enable_service (§8). The
 * empty /etc/samba/smb.conf is seeded so smbd starts. Real-hardware concern (§9).
 * The half people forget: cups alone prints to a queue you have no GUI to create.
 * system-config-printer is that GUI; gutenprint/hplip are the driver sets for
 * everything that is not driverless; cups-pdf gives a "Print to file" queue.
 * Driverless network printers additionally need mDNS - modules/avahi.sh.
 * Scanning: sane is the backend, sane-airscan adds driverless (eSCL/WSD) network
 * scanners, simple-scan is the GUI.
 * captdriver is the Canon CAPT vendor driver and is packaged almost nowhere
 * (AUR on Arch, absent on Void/Debian/Alpine). Everything except a CAPT-only
 * Canon prints fine without it, so a missing package must degrade to a warning
 * instead of aborting the whole rice (§9). The subshell contains error()'s exit.
 *
 * Port of modules/printer.sh, kept as the reference at
 * test/ref/printer_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_printer(void) {
    static const char *const smb[]     = { "smbclient", "cups", "samba", NULL };
    static const char *const drivers[] = {
        "cups-pdf", "system-config-printer", "gutenprint", "hplip", NULL
    };
    static const char *const scan[]    = { "sane", "sane-airscan", "simple-scan", NULL };
    static const char *const canon[]   = { "captdriver", NULL };
    char *argv[4];
    int ok;

    ok = osr_pkg_install_step("Installing printing + SMB", smb);
    ok = osr_pkg_install_step("Installing printer drivers + GUI", drivers) && ok;
    ok = osr_pkg_install_step("Installing scanner support", scan) && ok;
    /* Optional: only Canon CAPT printers need it, and most distros do not carry
     * it at all - so its absence must not take the rest of printing down. */
    if (!osr_pkg_install_step_try("Installing Canon captdriver", canon))
        osr_warn("captdriver is not available on this distro - skipping "
                 "(only Canon CAPT printers need it)");

    /* samba refuses to start without one, and ships none on some distros. */
    if (!file_exists("/etc/samba/smb.conf")) {
        argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
        argv[2] = (char *)"/etc/samba"; argv[3] = NULL;
        (void)osr_run_root(argv);
        argv[0] = (char *)"touch"; argv[1] = (char *)"/etc/samba/smb.conf";
        argv[2] = NULL;
        (void)osr_run_root(argv);
    }
    ok = osr_service_enable("smb") && ok;
    return osr_service_enable("cups") && ok;
}
