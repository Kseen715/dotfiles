/* modules/sddm.c -- SDDM display manager + the rice's "glass" QML theme. ONE copy,
 * POSIX (was .../modules/sddm.sh). Theme + conf live in system paths (as_root);
 * the service is enabled through enable_service (§8). DE/display module: installs
 * and lays down files in a container, but only a real display exercises it (§9).
 *
 * Was modules/sddm.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_sddm(void) {
    static const char *const pkgs[] = {
        "sddm", "qt6-5compat", "qt6-declarative", "qt6-svg", NULL
    };
    Str src, theme;
    char *argv[5];
    int ok;

    ok = osr_pkg_install_step("Installing SDDM", pkgs);

    /* SDDM's config and its theme are ROOT-owned by nature: the greeter runs
     * before any user session exists, so these are the one theme layer that
     * does not land in $HOME. */
    if (*osr_mod_theme_dir() != '\0') {
        str_init(&src); str_init(&theme);
        argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
        argv[2] = (char *)"/etc/sddm.conf.d"; argv[3] = NULL;
        (void)osr_run_root(argv);

        str_addz(&src, osr_mod_theme_dir());
        str_addz(&src, "/config/sddm/hyprland.main.conf");
        if (file_exists(str_text(&src))) {
            argv[0] = (char *)"cp"; argv[1] = (char *)"-f"; argv[2] = src.p;
            argv[3] = (char *)"/etc/sddm.conf.d/sddm.conf"; argv[4] = NULL;
            (void)osr_run_root(argv);
        }
        str_reset(&src);
        str_addz(&src, osr_mod_theme_dir());
        str_addz(&src, "/config/sddm/theme.conf.user");
        if (file_exists(str_text(&src))) {
            argv[0] = (char *)"cp"; argv[1] = (char *)"-f"; argv[2] = src.p;
            argv[3] = (char *)"/etc/sddm.conf.d/theme.conf.user"; argv[4] = NULL;
            (void)osr_run_root(argv);
        }
        str_addz(&theme, osr_mod_theme_dir());
        str_addz(&theme, "/config/sddm/glass-theme");
        if (dir_exists(str_text(&theme))) {
            argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
            argv[2] = (char *)"/usr/share/sddm/themes"; argv[3] = NULL;
            (void)osr_run_root(argv);
            argv[0] = (char *)"cp"; argv[1] = (char *)"-rf"; argv[2] = theme.p;
            argv[3] = (char *)"/usr/share/sddm/themes/"; argv[4] = NULL;
            (void)osr_run_root(argv);
        }
        str_free(&src); str_free(&theme);
    }
    return osr_service_enable("sddm") && ok;
}
