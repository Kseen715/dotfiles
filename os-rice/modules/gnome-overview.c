/* modules/gnome-overview.c -- free the bare Super key in a GNOME session.
 *
 * Tapping Super alone opens the Activities overview, and that overview is the
 * slowest thing in the session: GNOME Shell animates every window into a
 * thumbnail grid and starts the app search provider chain before the keypress
 * feels answered. It also swallows the tap for every other use of the key.
 *
 * org.gnome.mutter overlay-key holds the keysym mutter watches for; the empty
 * string means "watch for nothing". Chords keep working - <Super>r and friends
 * are separate bindings, so a launcher bound there is unaffected. The overview
 * itself is not removed, only the tap-to-open: <Super>s and the Activities
 * corner still reach it.
 *
 * No package: mutter is the GNOME session. Inert outside GNOME.
 *
 * Was modules/gnome-overview.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/gnome.h"

#include <stddef.h>

int osrm_gnome_overview(void) {
    char *argv[6];

    /* Only under GNOME: the Super-tap overview is a mutter setting, and setting
     * it anywhere else writes a key nothing reads. */
    if (!osr_gnome_is_session()) return 1;
    argv[0] = (char *)"gsettings"; argv[1] = (char *)"set";
    argv[2] = (char *)"org.gnome.mutter"; argv[3] = (char *)"overlay-key";
    argv[4] = (char *)""; argv[5] = NULL;
    return osr_run_step_user("Disabling the Super-tap overview", argv);
}
