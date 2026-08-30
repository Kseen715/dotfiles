/* modules/cliphist.c -- cliphist clipboard history + wofi image preview helper.
 * ONE copy, POSIX. ripgrep backs the search; the wofi image thumbnailer is a
 * small upstream script fetched to /usr/local/bin. go is a build prerequisite
 * for cliphist-wofi-img -- installed on demand (§4: order is the dependency
 * graph, but this module self-heals if go is absent).
 *
 * GNOME/Wayland: autostarts the cliphist store daemon via ~/.config/autostart
 * and registers a Super+V (Win+V) custom shortcut to open wofi clip history.
 *
 * Was modules/cliphist.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/fetch.h"
#include "../lib/gnome.h"

#include <stddef.h>
#include <unistd.h>

#define WOFI_IMG_URL "https://raw.githubusercontent.com/sentriz/cliphist/refs/heads/master/contrib/cliphist-wofi-img"

/* The autostart entry: the cliphist store daemon watches both the text and the
 * image clipboard, which is two `wl-paste --watch` processes and therefore a
 * shell rather than a bare Exec. */
static const char *const AUTOSTART_DESKTOP =
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=Cliphist Store\n"
    "Comment=Wayland clipboard history daemon\n"
    "Exec=sh -c 'wl-paste --type text --watch cliphist store & wl-paste --type image --watch cliphist store & wait'\n"
    "X-GNOME-Autostart-enabled=true\n"
    "NoDisplay=true\n";

int osrm_cliphist(void) {
    static const char *const pkgs[] = { "cliphist", "ripgrep", "wofi", "wl-clipboard", NULL };
    static const char *const go_pkg[] = { "go", NULL };
    Str src, dst, tmp;
    char *argv[6];
    int ok;

    ok = osr_pkg_install_step("Installing cliphist", pkgs);

    /* ---- Hyprland: themed start script ----------------------------------- */
    str_init(&src); str_init(&dst);
    if (*osr_mod_theme_dir() != '\0') {
        str_addz(&src, osr_mod_theme_dir());
        str_addz(&src, "/config/hypr/start-cliphist-store.sh");
        if (file_exists(str_text(&src))) {
            str_addz(&dst, osr_mod_home());
            str_addz(&dst, "/.config/hypr/start-cliphist-store.sh");
            ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
            argv[0] = (char *)"chmod"; argv[1] = (char *)"+x"; argv[2] = dst.p;
            argv[3] = NULL;
            (void)osr_run_user(argv);
        }
    }

    /* ---- cliphist-wofi-img helper ---------------------------------------- */
    if (!osr_have_cmd("go")) (void)osr_pkg_install(go_pkg);
    argv[0] = (char *)"go"; argv[1] = (char *)"install";
    argv[2] = (char *)"github.com/pdf/cliphist-wofi-img@latest"; argv[3] = NULL;
    ok = osr_run_step_user("Installing cliphist-wofi-img (go)", argv) && ok;

    /* Upstream wofi image-preview shim to /usr/local/bin (a system path, so
     * as_root). */
    if (access("/usr/local/bin/cliphist-wofi-img", X_OK) != 0) {
        str_init(&tmp);
        str_addz(&tmp, env_str("TMPDIR", "/tmp"));
        str_addz(&tmp, "/cliphist-wofi-img");
        if (osr_fetch_download(WOFI_IMG_URL, str_text(&tmp), 0)) {
            argv[0] = (char *)"install"; argv[1] = (char *)"-m"; argv[2] = (char *)"0755";
            argv[3] = tmp.p; argv[4] = (char *)"/usr/local/bin/cliphist-wofi-img";
            argv[5] = NULL;
            (void)osr_run_root(argv);
            (void)unlink(str_text(&tmp));
        } else {
            osr_warn("failed to fetch cliphist-wofi-img shim - skipping");
        }
        str_free(&tmp);
    }

    str_reset(&dst);
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.cache/cliphist/thumbs");
    ok = osr_mkdir_p(str_text(&dst)) && ok;

    /* ---- GNOME: autostart daemon + Super+V shortcut ----------------------
     * Keybinding helpers live in lib/gnome.c (shared with wofi's Super+R).
     * Super+V is stock GNOME's message-tray/calendar toggle, and a Shell
     * binding wins over a custom one, so it has to be freed before ours is
     * registered. */
    if (osr_gnome_is_session()) {
        Str autostart;
        str_init(&autostart);
        str_addz(&autostart, osr_mod_home()); str_addz(&autostart, "/.config/autostart");
        argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p"; argv[2] = autostart.p;
        argv[3] = NULL;
        ok = osr_run_step_user("cliphist GNOME autostart", argv) && ok;

        str_reset(&dst);
        str_addz(&dst, str_text(&autostart));
        str_addz(&dst, "/cliphist-store.desktop");
        if (!file_exists(str_text(&dst)))
            ok = osr_write_user(str_text(&dst), AUTOSTART_DESKTOP) && ok;
        str_free(&autostart);

        osr_info("cliphist unbind Super+V from GNOME Shell");
        (void)osr_gnome_free_binding("<Super>v");
        osr_info("cliphist Super+V shortcut");
        (void)osr_gnome_keybind("cliphist", "Clipboard History", "<Super>v",
                                "cliphist-wofi-img | wl-copy");
    }

    /* ---- wofi config (theme-aware) --------------------------------------- */
    if (*osr_mod_theme_dir() != '\0') ok = osr_apply_config("wofi") && ok;

    str_free(&src); str_free(&dst);
    return ok;
}
