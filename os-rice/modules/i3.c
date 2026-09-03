/* modules/i3.c -- i3 window manager (X11) + layered config. ONE copy, POSIX.
 * pacman ships it as i3-wm (pacman.map); Void and Debian call it i3.
 *
 * Config is split by ownership (§5), the same shape as ghostty/foot:
 *
 *   ~/.config/i3/config              dotfiles-owned (10-layer) -- keybinds,
 *                                    rules, autostart. Overwritten on update.
 *   ~/.config/i3/config.d/90-theme.conf
 *                                    rice-owned (90-layer) -- colors, gaps,
 *                                    font, bar. Swapped on rice switch (§6).
 *   ~/.config/i3/config.d/99-local.conf
 *                                    machine-owned, seeded empty, never touched.
 *
 * The base config ends with an `include` of the *.conf files under
 * ~/.config/i3/config.d/, so the theme
 * layer swaps independently of the keybinds (i3 >= 4.20 has `include`, and it
 * glob-expands the path).
 *
 * Companions installed here are the ones the shipped config actually invokes:
 * i3status (fallback bar if polybar dies), dex (XDG autostart -- i3 runs none of
 * it by itself, §3.8), numlockx, autotiling (dwindle-style splits), xclip (every
 * screenshot/clipboard binding).
 *
 * unclutter-xfixes, not unclutter: they are different programs with incompatible
 * flags, and BOTH are packaged on Void and on Debian/Ubuntu. The config runs
 * `unclutter --timeout 3`, which is the xfixes fork's syntax; the original wants
 * `-idle 3` and would exit with a usage error nobody sees, leaving the pointer
 * sitting in the middle of the text you are reading.
 *
 * Was modules/i3.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/render.h"

#include <stddef.h>
#include <unistd.h>

int osrm_i3(void) {
    static const char *const pkgs[] = {
        "i3", "i3status", "dex", "numlockx", "autotiling", "unclutter-xfixes",
        "xclip", NULL
    };
    /* The helper scripts the bindings call: power menu, volume/brightness OSD. */
    static const char *const scripts[] = { "rofi-powermenu.sh", "osd.sh", "layout.sh", "min-tile.py", NULL };
    Str dir, src, dst, layer;
    char *argv[6];
    size_t i;
    int is_temp = 0;
    int ok;

    ok = osr_pkg_install_step("Installing i3", pkgs);

    str_init(&dir);
    str_addz(&dir, osr_mod_home()); str_addz(&dir, "/.config/i3/config.d");
    ok = osr_mkdir_p(str_text(&dir)) && ok;

    str_init(&src); str_init(&dst);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/i3/.config/i3/config");
    str_addz(&dst, osr_mod_home());     str_addz(&dst, "/.config/i3/config");
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    for (i = 0; scripts[i] != NULL; i++) {
        str_reset(&src); str_reset(&dst);
        str_addz(&src, osr_mod_dotfiles());
        str_addz(&src, "/i3/.config/i3/scripts/"); str_addz(&src, scripts[i]);
        str_addz(&dst, osr_mod_home());
        str_addz(&dst, "/.config/i3/scripts/");    str_addz(&dst, scripts[i]);
        if (!file_exists(str_text(&src))) continue;
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
        argv[0] = (char *)"chmod"; argv[1] = (char *)"+x"; argv[2] = dst.p; argv[3] = NULL;
        (void)osr_run_user(argv);
    }

    /* The terminal launcher goes on PATH as `osr-term`, not into ~/.config/i3,
     * for one reason: rofi's `terminal:` value is TOKENIZED, not run through a
     * shell, so a "~/.config/..." path there resolves to nothing and every
     * "open in terminal" in rofi dies silently. One name every consumer can
     * spell -- i3's $term, rofi, and the xfce4 helpers.rc that helpers.c
     * seeds -- beats three paths. */
    str_reset(&src);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/i3/.config/i3/scripts/term.sh");
    if (file_exists(str_text(&src))) {
        argv[0] = (char *)"install"; argv[1] = (char *)"-m"; argv[2] = (char *)"0755";
        argv[3] = src.p; argv[4] = (char *)"/usr/local/bin/osr-term"; argv[5] = NULL;
        ok = osr_run_step_root("Installing the terminal launcher (osr-term)", argv) && ok;
    }

    /* Theme layer (rice-owned, swapped on switch §6). Two substitutions, in
     * order: the palette (§6b), then the wallpaper path - the layer carries
     * {{WALLPAPER_PATH}} as well as color roles. */
    str_init(&layer);
    if (osr_theme_source(&layer, "i3", "90-theme.conf", &is_temp)) {
        str_reset(&dst);
        str_addz(&dst, str_text(&dir)); str_addz(&dst, "/90-theme.conf");
        ok = osr_install_wallpaper_layer(str_text(&layer), str_text(&dst)) && ok;
        if (is_temp) (void)unlink(str_text(&layer));
    }
    str_free(&layer);

    /* Machine layer -- yours, never rewritten. */
    str_reset(&dst);
    str_addz(&dst, str_text(&dir)); str_addz(&dst, "/99-local.conf");
    ok = osr_seed_empty(str_text(&dst)) && ok;

    str_free(&dir); str_free(&src); str_free(&dst);
    return ok;
}
