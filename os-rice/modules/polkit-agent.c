/* modules/polkit-agent.c -- the polkit authentication agent (i3-sugg §3.1).
 * Mandatory and silent when missing: with no agent running, every GUI action
 * that needs root — mounting an internal disk, printer setup, virt-manager,
 * blueman pairing, timeshift — fails with no dialog and no error.
 *
 * i3 starts no agent by itself; the i3 config execs the binary path below.
 * polkit-gnome is the classic single-binary choice (mate-polkit and
 * lxqt-policykit are drop-in alternatives, see i3-sugg §3.1).
 * The agent lives at a different path per distro; report the resolved one so a
 * wrong `exec` line in the i3 config is obvious instead of mysterious.
 *
 * Was modules/polkit-agent.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <unistd.h>

int osrm_polkit_agent(void) {
    static const char *const pkgs[] = { "polkit", "polkit-gnome", NULL };
    /* The same binary, under whichever path this distro puts libexec at. */
    static const char *const paths[] = {
        "/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1",
        "/usr/libexec/polkit-gnome-authentication-agent-1",
        "/usr/lib/x86_64-linux-gnu/polkit-gnome/polkit-gnome-authentication-agent-1",
        NULL
    };
    size_t i;
    int ok;

    ok = osr_pkg_install_step("Installing polkit agent", pkgs);
    for (i = 0; paths[i] != NULL; i++) {
        if (access(paths[i], X_OK) == 0) {
            osr_infof("polkit agent: %s", paths[i]);
            return ok;
        }
    }
    /* Reported, not fixed: the exec line lives in the i3 config, and guessing a
     * path that is not there would only move the failure later. */
    osr_warn("polkit agent binary not found - check the exec line in "
             "~/.config/i3/config");
    return ok;
}
