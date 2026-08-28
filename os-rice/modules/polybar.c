/* modules/polybar.c -- polybar status bar, the X11 replacement for waybar
 * (i3-sugg §2). Config split (§5): config.ini + modules.ini are dotfiles-owned
 * (bar geometry, module definitions, the `include-file` lines) and colors.ini is
 * rice-owned, so a rice switch repaints the bar without touching its layout.
 *
 * Companions the shipped modules invoke: pamixer (volume), playerctl (MPRIS),
 * brightnessctl (backlight), lm_sensors (temps), gsimplecal (date popup). They
 * are listed here rather than in the config so a missing binary is an install
 * error, not a silently blank module.
 * Module helper scripts. The custom/script modules in modules.ini name these by
 * absolute path, so a bar whose scripts did not land shows empty modules rather
 * than an error - install them with the config, not separately.
 *
 * Port of modules/polybar.sh, kept as the reference at
 * test/ref/polybar_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <dirent.h>
#include <string.h>

int osrm_polybar(void) {
    static const char *const pkgs[] = {
        "polybar", "pamixer", "playerctl", "lm_sensors", "gsimplecal", NULL
    };
    static const char *const files[] = { "config.ini", "modules.ini", "launch.sh", NULL };
    Str dir, src, dst;
    char *argv[4];
    size_t i;
    int ok;

    ok = osr_pkg_install_step("Installing polybar", pkgs);

    str_init(&dir); str_init(&src); str_init(&dst);
    str_addz(&dir, osr_mod_home()); str_addz(&dir, "/.config/polybar");
    ok = osr_mkdir_p(str_text(&dir)) && ok;

    for (i = 0; files[i] != NULL; i++) {
        str_reset(&src); str_reset(&dst);
        str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/polybar/");
        str_addz(&src, files[i]);
        str_addz(&dst, str_text(&dir)); str_addc(&dst, '/'); str_addz(&dst, files[i]);
        if (file_exists(str_text(&src)))
            ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    }
    str_reset(&dst);
    str_addz(&dst, str_text(&dir)); str_addz(&dst, "/launch.sh");
    if (file_exists(str_text(&dst))) {
        argv[0] = (char *)"chmod"; argv[1] = (char *)"+x"; argv[2] = dst.p; argv[3] = NULL;
        (void)osr_run_user(argv);
    }

    /* The bar's own modules are scripts, and a script that is not executable is
     * a bar segment that silently stays empty. */
    str_reset(&src);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/polybar/scripts");
    if (dir_exists(str_text(&src))) {
        DIR *d;
        struct dirent *e;
        Str sdir;

        str_init(&sdir);
        str_addz(&sdir, str_text(&dir)); str_addz(&sdir, "/scripts");
        ok = osr_mkdir_p(str_text(&sdir)) && ok;
        d = opendir(str_text(&src));
        if (d != NULL) {
            while ((e = readdir(d)) != NULL) {
                size_t n = strlen(e->d_name);
                Str from, to;
                if (n < 4 || strcmp(e->d_name + n - 3, ".sh") != 0) continue;
                str_init(&from); str_init(&to);
                str_addz(&from, str_text(&src)); str_addc(&from, '/');
                str_addz(&from, e->d_name);
                str_addz(&to, str_text(&sdir)); str_addc(&to, '/');
                str_addz(&to, e->d_name);
                if (file_exists(str_text(&from))) {
                    ok = osr_install_layer(str_text(&from), str_text(&to)) && ok;
                    argv[0] = (char *)"chmod"; argv[1] = (char *)"+x";
                    argv[2] = to.p; argv[3] = NULL;
                    (void)osr_run_user(argv);
                }
                str_free(&from); str_free(&to);
            }
            closedir(d);
        }
        str_free(&sdir);
    }
    str_reset(&dst);
    str_addz(&dst, str_text(&dir)); str_addz(&dst, "/colors.ini");
    (void)osr_install_theme_layer("polybar", "colors.ini", str_text(&dst));

    str_free(&dir); str_free(&src); str_free(&dst);
    return ok;
}
