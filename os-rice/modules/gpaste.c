/* modules/gpaste.c -- GPaste, the clipboard manager for GNOME sessions.
 *
 * GNOME-only by nature, not by preference: GPaste's daemon has no clipboard of
 * its own to watch on Wayland. It reads the selection through the GNOME Shell
 * extension, and the extension is also what grabs its hotkeys. That is why this
 * module cares so much about the extension actually being enabled — a GPaste
 * whose extension is off is not a degraded GPaste, it is an empty one.
 *
 * Both sessions, because GNOME runs both — but the modules that own the non-GNOME
 * clipboard on each are cliphist.sh (Wayland/Hyprland) and copyq.sh (X11), and
 * the GNOME-specific half below is gated so this module is inert next to them.
 *
 * The build lives in lib/build.sh (apt.map -> source:provide_gpaste): the distro
 * package is version-mismatched against the Shell and has to be replaced, not
 * configured around.
 *
 * Was modules/gpaste.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/gnome.h"

#include <stddef.h>
#include <stdlib.h>

#define GPASTE_UUID "GPaste@gnome-shell-extensions.gnome.org"

/* enable_extension -- a live session bus is what `gnome-extensions enable`
 * needs, and an installer run over ssh or from a TTY does not have one. Not
 * fatal: the settings below still land in dconf, and the extension can be
 * switched on from the Extensions app afterwards. */
static int enable_extension(void *ctx) {
    char *argv[4];
    (void)ctx;
    argv[0] = (char *)"gnome-extensions"; argv[1] = (char *)"enable";
    argv[2] = (char *)GPASTE_UUID; argv[3] = NULL;
    if (osr_run_user_quiet(argv) == 0) return 1;
    osr_warn("could not enable the GPaste extension now - turn it on in the "
             "Extensions app (GPaste has no clipboard access without it)");
    return 1;
}

int osrm_gpaste(void) {
    static const char *const pkgs[] = { "gpaste", NULL };
    /* setting, value: images-support is what makes a copied image enter the
     * history at all, and it is the one that looks enabled-but-dead under a
     * mismatched extension - the daemon never sees the image to store. Setting
     * it again after the version fix is what makes it take effect. The memory
     * cap is upstream's 30 MiB default raised to roughly a dozen screenshots. */
    static const char *const settings[] = {
        "images-support", "true",
        "rich-text-support", "true",
        "max-memory-usage", "200",
        NULL
    };
    char *argv[6];
    size_t i;
    int ok;

    ok = osr_pkg_install_step("Installing GPaste", pkgs);
    if (!osr_gnome_is_session()) return ok;

    ok = osr_step("Enabling the GPaste shell extension", enable_extension, NULL) && ok;
    for (i = 0; settings[i] != NULL; i += 2) {
        argv[0] = (char *)"gsettings"; argv[1] = (char *)"set";
        argv[2] = (char *)"org.gnome.GPaste"; argv[3] = (char *)settings[i];
        argv[4] = (char *)settings[i + 1]; argv[5] = NULL;
        (void)osr_run_user(argv);
    }
    /* Pick up the new schema, D-Bus service and typelib: the running daemon is
     * still the old build until it is told otherwise. */
    argv[0] = (char *)"systemctl"; argv[1] = (char *)"--user";
    argv[2] = (char *)"daemon-reload"; argv[3] = NULL;
    (void)osr_run_user_quiet(argv);
    argv[2] = (char *)"restart"; argv[3] = (char *)"org.gnome.GPaste.service";
    argv[4] = NULL;
    (void)osr_run_user_quiet(argv);
    return ok;
}
