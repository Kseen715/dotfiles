/* modules/hyprcursor.c -- hyprcursor + Bibata cursor theme (AUR). ONE copy, POSIX
 * (was .../modules/hyprcursor.sh). The theme is copied into the user's icon dir;
 * gsettings/flatpak overrides are best-effort (only when those tools exist).
 *
 * Was modules/hyprcursor.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_hyprcursor(void) {
    static const char *const pkgs[]   = { "hyprcursor", NULL };
    static const char *const bibata[] = { "bibata-cursor-theme", NULL };
    Str icons;
    char *argv[8];
    int ok;

    ok = osr_pkg_install_step("Installing hyprcursor", pkgs);
    ok = osr_pkg_install_step("Installing Bibata cursor theme (AUR)", bibata) && ok;

    str_init(&icons);
    str_addz(&icons, osr_mod_home());
    str_addz(&icons, "/.local/share/icons");
    ok = osr_mkdir_p(str_text(&icons)) && ok;
    /* A copy into the user's icon dir, not a symlink: a flatpak reads it through
     * the --filesystem grant below, and that does not follow links out of it. */
    if (dir_exists("/usr/share/icons/Bibata-Modern-Ice")) {
        Str dst;
        str_init(&dst);
        str_addz(&dst, str_text(&icons));
        str_addc(&dst, '/');
        argv[0] = (char *)"cp"; argv[1] = (char *)"-rf";
        argv[2] = (char *)"/usr/share/icons/Bibata-Modern-Ice";
        argv[3] = dst.p; argv[4] = NULL;
        (void)osr_run_user(argv);
        str_free(&dst);
    }
    /* Cosmetic, and only where the tool exists: a bare WM has no gsettings and
     * no flatpak, and neither is worth failing the module over. */
    if (osr_have_cmd("gsettings")) {
        argv[0] = (char *)"gsettings"; argv[1] = (char *)"set";
        argv[2] = (char *)"org.gnome.desktop.interface";
        argv[3] = (char *)"cursor-theme"; argv[4] = (char *)"Bibata-Modern-Ice";
        argv[5] = NULL;
        (void)osr_run_user_quiet(argv);
        argv[3] = (char *)"cursor-size"; argv[4] = (char *)"24";
        (void)osr_run_user_quiet(argv);
    }
    if (osr_have_cmd("flatpak")) {
        Str themes, share;
        str_init(&themes); str_init(&share);
        str_addz(&themes, "--filesystem="); str_addz(&themes, osr_mod_home());
        str_addz(&themes, "/.themes:ro");
        str_addz(&share, "--filesystem="); str_addz(&share, str_text(&icons));
        str_addz(&share, ":ro");
        argv[0] = (char *)"flatpak"; argv[1] = (char *)"override";
        argv[2] = (char *)"--user"; argv[3] = themes.p; argv[4] = share.p;
        argv[5] = NULL;
        (void)osr_run_user_quiet(argv);
        str_free(&themes); str_free(&share);
    }
    str_free(&icons);
    return ok;
}
