/* modules/hyprland.c -- Hyprland compositor + session wiring. ONE copy, POSIX
 * (was .../modules/hyprland.sh, ~66 lines of bash+chown boilerplate). Package
 * install goes through pkg_install; config is rice-owned (§5/§6) and copied via
 * the framework's as_user/install_layer helpers instead of hand-rolled
 * sudo -u + chown. The wayland-session launchers land in a system path, so they
 * are copied as_root. DE-runtime module: installs in a container, but only a real
 * GPU/display exercises it (§9).
 * User dirs the session expects (idempotent; owned by OSR_USER via as_user).
 * Rice-owned config: main hyprland.conf, autostart scripts, and the qt6ct theme.
 *
 * Was modules/hyprland.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int osrm_hyprland(void) {
    static const char *const pkgs[] = {
        "hyprland", "hyprshot", "xdg-desktop-portal-hyprland", "hyprland-qt-support",
        "hypridle", "hyprutils", "aquamarine", "hyprgraphics", "hyprland-qtutils",
        "hyprpolkitagent", "qt6ct", "pop-gtk-theme", NULL
    };
    /* Every autostart script hyprland.conf's exec-once lines reference. Each
     * script guards on its own binary, so installing one whose module was not
     * selected is inert. (start-cliphist-store.sh is cliphist's, installed there
     * so `osr module cliphist` alone still lands it.) */
    static const char *const autostart[] = {
        "start-audio", "start-amnezia-vpn-client", "start-mako", "start-easyeffects",
        "start-top", "start-wleave", NULL
    };
    static const char *const dirs[] = {
        "/.config/hypr", "/Downloads", "/Pictures", "/.local/share", NULL
    };
    Str hd, wd, src, dst;
    Str paths[4];
    const char *made[5];
    char *argv[5];
    size_t i;
    int ok;

    ok = osr_pkg_install_step("Installing Hyprland", pkgs);

    /* One mkdir for the four: the session's own config dir plus the three the
     * autostart scripts and screenshot bindings write into. */
    str_init(&src); str_init(&dst);
    for (i = 0; i < 4; i++) {
        str_init(&paths[i]);
        str_addz(&paths[i], osr_mod_home());
        str_addz(&paths[i], dirs[i]);
        made[i] = str_text(&paths[i]);
    }
    made[4] = NULL;
    ok = osr_mkdir_p_all(made) && ok;
    for (i = 0; i < 4; i++) str_free(&paths[i]);

    if (*osr_mod_theme_dir() == '\0') { str_free(&src); str_free(&dst); return ok; }

    str_init(&hd); str_init(&wd);
    str_addz(&hd, osr_mod_theme_dir()); str_addz(&hd, "/config/hypr");
    str_addz(&wd, osr_mod_theme_dir()); str_addz(&wd, "/config/wayland-sessions");

    /* hyprland.conf exports `env = WALLPAPER_PATH,{{WALLPAPER_PATH}}` for the
     * session; the placeholder resolves to the same installed file hyprpaper and
     * gtklock paint. */
    str_reset(&src); str_reset(&dst);
    str_addz(&src, str_text(&hd)); str_addz(&src, "/hyprland.conf");
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.config/hypr/hyprland.conf");
    if (file_exists(str_text(&src)))
        ok = osr_install_wallpaper_layer(str_text(&src), str_text(&dst)) && ok;

    for (i = 0; autostart[i] != NULL; i++) {
        str_reset(&src); str_reset(&dst);
        str_addz(&src, str_text(&hd)); str_addc(&src, '/');
        str_addz(&src, autostart[i]); str_addz(&src, ".sh");
        str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.config/hypr/");
        str_addz(&dst, autostart[i]); str_addz(&dst, ".sh");
        if (!file_exists(str_text(&src))) continue;
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
        argv[0] = (char *)"chmod"; argv[1] = (char *)"+x"; argv[2] = dst.p; argv[3] = NULL;
        (void)osr_run_user(argv);
    }

    str_reset(&src); str_reset(&dst);
    str_addz(&src, osr_mod_theme_dir()); str_addz(&src, "/config/qt6ct/qt6ct.conf");
    str_addz(&dst, osr_mod_home());      str_addz(&dst, "/.config/qt6ct/qt6ct.conf");
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    /* The session launcher lives in a system path SDDM reads. It stays
     * root-owned and world-executable (0755) - the legacy config chowned it to
     * the target user so "sddm can run it", which SDDM never needed. */
    str_reset(&src);
    str_addz(&src, str_text(&wd)); str_addz(&src, "/hyprland.desktop");
    if (file_exists(str_text(&src))) {
        Str launcher;
        argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
        argv[2] = (char *)"/usr/share/wayland-sessions"; argv[3] = NULL;
        (void)osr_run_root(argv);
        argv[0] = (char *)"cp"; argv[1] = (char *)"-f"; argv[2] = src.p;
        argv[3] = (char *)"/usr/share/wayland-sessions/hyprland.desktop"; argv[4] = NULL;
        (void)osr_run_root(argv);
        str_init(&launcher);
        str_addz(&launcher, str_text(&wd)); str_addz(&launcher, "/start-hyprland.sh");
        argv[2] = launcher.p;
        argv[3] = (char *)"/usr/share/wayland-sessions/start-hyprland.sh";
        (void)osr_run_root(argv);
        argv[0] = (char *)"chmod"; argv[1] = (char *)"0755";
        argv[2] = (char *)"/usr/share/wayland-sessions/start-hyprland.sh"; argv[3] = NULL;
        (void)osr_run_root(argv);
        str_free(&launcher);
    }

    /* A second session entry for a VMware guest: the launcher adds the software
     * renderer and cursor workarounds Hyprland needs without a real GPU. Offered
     * ALONGSIDE the normal entry rather than replacing it, so the greeter still
     * lists both (§9: a VM-only path, verified on hardware). */
    str_reset(&src);
    str_addz(&src, str_text(&wd)); str_addz(&src, "/hyprland-vmware.desktop");
    if (strcmp(env_str("OSR_VIRT", "none"), "vmware") == 0 && file_exists(str_text(&src))) {
        Str launcher;
        argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
        argv[2] = (char *)"/usr/share/wayland-sessions"; argv[3] = NULL;
        (void)osr_run_root(argv);
        argv[0] = (char *)"cp"; argv[1] = (char *)"-f"; argv[2] = src.p;
        argv[3] = (char *)"/usr/share/wayland-sessions/hyprland-vmware.desktop";
        argv[4] = NULL;
        (void)osr_run_root(argv);
        str_init(&launcher);
        str_addz(&launcher, str_text(&wd));
        str_addz(&launcher, "/start-hyprland-vmware.sh");
        argv[2] = launcher.p;
        argv[3] = (char *)"/usr/share/wayland-sessions/start-hyprland-vmware.sh";
        (void)osr_run_root(argv);
        argv[0] = (char *)"chmod"; argv[1] = (char *)"0755";
        argv[2] = (char *)"/usr/share/wayland-sessions/start-hyprland-vmware.sh";
        argv[3] = NULL;
        (void)osr_run_root(argv);
        str_free(&launcher);
    }
    str_free(&hd); str_free(&wd); str_free(&src); str_free(&dst);
    return ok;
}
