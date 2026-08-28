/* modules/keyring.c -- Secret Service on D-Bus (i3-sugg §3.5). VS Code, Chrome,
 * the git credential helper, Nextcloud and Element all expect one; without it
 * they either nag on every start or silently store nothing.
 *
 * gnome-keyring provides the daemon, libsecret the client API, gcr the prompt UI,
 * seahorse the GUI. The PAM lines are what make the keyring unlock with your
 * login password instead of asking again — they are appended to the DM's and the
 * console's PAM stacks only if absent (idempotent, §2).
 * PAM wiring. `optional` on purpose: a broken keyring must never lock you out.
 *
 * Port of modules/keyring.sh, kept as the reference at
 * test/ref/keyring_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <string.h>

int osrm_keyring(void) {
    static const char *const pkgs[] = {
        "gnome-keyring", "libsecret", "gcr", "seahorse", NULL
    };
    static const char *const stacks[] = {
        "/etc/pam.d/lightdm", "/etc/pam.d/login", "/etc/pam.d/sddm", NULL
    };
    static const char lines[] =
        "auth       optional  pam_gnome_keyring.so\n"
        "session    optional  pam_gnome_keyring.so auto_start\n";
    size_t i;
    int ok;

    ok = osr_pkg_install_step("Installing keyring", pkgs);

    /* The login stacks that exist on this box, and only the ones that do not
     * already wire it: appending twice would ask for the password twice. */
    for (i = 0; stacks[i] != NULL; i++) {
        char *buf;
        size_t len;
        int wired;

        buf = slurp(stacks[i], &len);
        if (buf == NULL) continue;
        wired = strstr(buf, "pam_gnome_keyring") != NULL;
        free(buf);
        if (wired) {
            osr_infof("%s already wires pam_gnome_keyring - skipping", stacks[i]);
            continue;
        }
        osr_infof("adding pam_gnome_keyring to %s", stacks[i]);
        ok = osr_append_root(stacks[i], lines) && ok;
    }
    return ok;
}
