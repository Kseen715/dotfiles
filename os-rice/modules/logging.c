/* modules/logging.c -- a system log that SURVIVES A CRASH. ONE copy, POSIX.
 *
 * Not a cosmetic module. A stock Void install logs the kernel ring to
 * /var/log/dmesg.log at every boot and overwrites it at the next one, and runs
 * no syslog daemon at all - so after a hard lock there is nothing to read: the
 * ring died with the machine and the file on disk describes the boot that came
 * AFTER the crash. That is the difference between "nouveau took the box down"
 * and "the box went down, cause unknown", and it is only fixable BEFORE the
 * crash, which is why this is an install-time module and not advice.
 *
 * Per init, because the missing piece differs:
 *   runit   - no syslog daemon at all: socklog (the socket reader) + nanoklogd
 *             (the kernel-ring reader). Void ships both as socklog-void.
 *   systemd - journald is already running but defaults to Storage=auto, which
 *             means volatile until /var/log/journal exists. Creating the
 *             directory is the documented way to make it persistent; the
 *             drop-in states the intent so the next reader is not left guessing
 *             whether the directory is deliberate.
 *   others  - openrc/sysvinit boxes ship a syslog daemon by default; say what
 *             was skipped rather than install a second one over it.
 *
 * What it must do is stated in the C tests under test/unit_c/. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <string.h>

#define JOURNALD_DROPIN "/etc/systemd/journald.conf.d/10-osr-persistent.conf"

int osrm_logging(void) {
    static const char *const socklog[] = { "socklog-void", NULL };
    const char *init = osr_mod_init();
    int ok = 1;

    if (strcmp(init, "runit") == 0) {
        /* _try, not the fatal form: `socklog-void` is Void's name for it, and
         * a runit box from another distro (Artix, Hyperbola) has neither that
         * package nor necessarily a pkgmap row for it yet. A rice must not die
         * over a log daemon it could not name -- and the service step below
         * already reports the consequence by name ("package ships no runit
         * service"), so the failure stays visible instead of silent. */
        ok = osr_pkg_install_step_try("Installing socklog (persistent system log)",
                                      socklog);
        /* Two services, not one: socklog-unix reads /dev/log (everything that
         * calls syslog()), nanoklogd reads /dev/kmsg (the kernel ring, which is
         * where a GPU or ACPI failure is recorded). Enabling only the first
         * leaves exactly the messages a crash investigation needs unlogged. */
        ok = osr_service_enable("socklog-unix") && ok;
        ok = osr_service_enable("nanoklogd") && ok;
        osr_info("kernel log now persists in /var/log/socklog/kernel/");
        return ok;
    }

    if (strcmp(init, "systemd") == 0) {
        char *argv[5];
        argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
        argv[2] = (char *)"/var/log/journal"; argv[3] = NULL;
        ok = osr_run_root(argv) == 0;
        argv[2] = (char *)"/etc/systemd/journald.conf.d";
        ok = (osr_run_root(argv) == 0) && ok;
        ok = osr_write_root(JOURNALD_DROPIN,
                            "# Managed by os-rice (modules/logging.c).\n"
                            "# Keep the journal across reboots, so the log of a crash outlives it.\n"
                            "[Journal]\n"
                            "Storage=persistent\n") && ok;
        argv[0] = (char *)"systemctl"; argv[1] = (char *)"restart";
        argv[2] = (char *)"systemd-journald"; argv[3] = NULL;
        (void)osr_run_root(argv);
        osr_info("journal is persistent - `journalctl -b -1` reads the boot before a crash");
        return ok;
    }

    osr_warnf("init '%s' ships its own syslog daemon - not installing a second one; "
              "check that /var/log survives a reboot before trusting it", init);
    return 1;
}
