/* modules/ufw.c -- host firewall (i3-sugg §7.1). ufw over raw nftables rules
 * because the rules a desktop needs are three lines, and gufw gives you a GUI
 * for the fourth.
 *
 * Policy is deliberately conservative and set only on a fresh install: deny
 * inbound, allow outbound. Rewriting an existing ruleset on every rerun would
 * silently undo whatever the machine's owner opened (§2 — never override user
 * state). The firewall is enabled, but that is the only mutation on a rerun.
 *
 * Note: this closes mDNS (5353/udp) and KDE Connect (1714-1764) by default. The
 * commented lines below are the two most people want back.
 *
 * Port of modules/ufw.sh, kept as the reference at
 * test/ref/ufw_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <string.h>

int osrm_ufw(void) {
    static const char *const pkgs[] = { "ufw", "gufw", NULL };
    char *argv[7];
    int ok;

    ok = osr_pkg_install_step("Installing ufw", pkgs);
    if (osr_have_cmd("ufw")) {
        Str status;
        int active;

        str_init(&status);
        argv[0] = (char *)"ufw"; argv[1] = (char *)"status"; argv[2] = NULL;
        (void)osr_run_root_capture(argv, &status);
        active = strstr(str_text(&status), "Status: active") != NULL;
        str_free(&status);

        if (active) {
            /* Never touch a ruleset somebody has already set up: this module
             * gives a box its FIRST policy, it does not own the firewall. */
            osr_info("ufw already active - leaving the existing ruleset alone");
        } else {
            osr_info("setting the default ufw policy (deny in, allow out)");
            argv[0] = (char *)"ufw"; argv[1] = (char *)"--force";
            argv[2] = (char *)"default"; argv[3] = (char *)"deny";
            argv[4] = (char *)"incoming"; argv[5] = NULL;
            if (osr_run_root(argv) != 0) osr_warn("ufw default deny incoming failed");
            argv[3] = (char *)"allow"; argv[4] = (char *)"outgoing";
            if (osr_run_root(argv) != 0) osr_warn("ufw default allow outgoing failed");
            /* Uncomment in a fork of this module if the desktop needs them:
             *   ufw allow 5353/udp            mDNS
             *   ufw allow 1714:1764/udp|tcp   KDE Connect */
            argv[1] = (char *)"--force"; argv[2] = (char *)"enable"; argv[3] = NULL;
            if (osr_run_root(argv) != 0) osr_warn("could not enable ufw");
        }
    }
    if (!osr_service_enable("ufw"))
        osr_warn("could not enable the ufw service (needs a real init)");
    return ok;
}
