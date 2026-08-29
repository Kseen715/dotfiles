/* modules/yazi.c -- Yazi file manager + layered config. ONE copy, POSIX,
 * distro-agnostic. Config split by ownership (§5):
 *
 *   yazi.toml     dotfiles-owned (10) -- keymaps/manager settings, rice-independent
 *   package.toml  dotfiles-owned (10) -- declared plugin/flavor set
 *   flavors/      dotfiles-owned       -- installed flavor programs (G5: not config)
 *   theme.toml    rice-owned theme (90) -- selects the flavor, swapped on switch
 *                 (§6); falls back to the dotfiles default when a rice ships none.
 *
 * Yazi's adapter ladder is kitty graphics -> iTerm2 inline images -> sixel ->
 * Ueberzug++ -> chafa (unicode half-blocks). Which one it reaches is decided by
 * the SESSION, not by what is installed (yazi-adapter/src/drivers/drivers.rs):
 * on X11 yazi returns the Ueberzug++ driver UNCONDITIONALLY, and on Wayland it
 * does the same under sway/Hyprland/niri/Wayfire. chafa is reached only with no
 * graphical session (SSH, tmux on a server) or on a compositor yazi does not
 * support -- so chafa is the headless safety net and Ueberzug++ is what makes
 * previews work on a desktop.
 *
 * Port of modules/yazi.sh, kept as the reference at
 * test/ref/yazi_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/build.h"

#include <stddef.h>

/* build_chafa -- the one builder this module calls by name rather than through
 * a pkgmap row. The rows route the releases known to be behind straight to
 * provide_chafa; this catches the rest -- a box that ALREADY had an old distro
 * chafa (which satisfies pkg_install's presence probe and would never be
 * replaced), an EOL release, or an admin-pinned package. */
static int build_chafa(void *ctx) {
    (void)ctx;
    return osr_build_run("provide_chafa");
}

/* needs_ueberzug -- Drivers::matches() in yazi, in its order, so the install
 * decision matches what yazi will actually pick at runtime. False on a headless
 * box (container, SSH, the test matrix), where nothing routes to Ueberzug++ and
 * chafa is the adapter -- so no desktop-only build happens there. */
static int needs_ueberzug(void) {
    /* The four compositors yazi's Ueberzug::supported_compositor() accepts. */
    int wl = env_is_set("NIRI_SOCKET") || env_is_set("SWAYSOCK")
          || env_is_set("HYPRLAND_INSTANCE_SIGNATURE") || env_is_set("WAYFIRE_SOCKET");
    const char *type = env_str("XDG_SESSION_TYPE", "");

    if (strcmp(type, "x11") == 0) return 1;
    if (strcmp(type, "wayland") == 0) return wl;
    if (env_is_set("WAYLAND_DISPLAY")) return wl;
    return env_is_set("DISPLAY");
}

int osrm_yazi(void) {
    static const char *const pkgs[] = { "yazi", "chafa", NULL };
    static const char *const ueberzug[] = { "ueberzugpp", NULL };
    Str src, dst, cfg;
    int ok;

    ok = osr_pkg_install_step("Installing Yazi", pkgs);
    if (!osr_chafa_ok())
        ok = osr_step("Building chafa >= " OSR_CHAFA_MIN " (yazi image previews)",
                      build_chafa, NULL) && ok;

    /* Ueberzug++ draws real pixels in a terminal with no graphics protocol of
     * its own, which is the entire reason an image preview works under
     * Alacritty on X11. Arch/Gentoo package it; everywhere else pkgmap routes
     * to provide_ueberzugpp. */
    if (needs_ueberzug())
        ok = osr_pkg_install_step("Installing Ueberzug++ (yazi image previews)",
                                  ueberzug) && ok;

    str_init(&cfg);
    str_addz(&cfg, osr_mod_home()); str_addz(&cfg, "/.config/yazi");

    str_init(&src); str_init(&dst);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/yazi/yazi.toml");
    str_addz(&dst, str_text(&cfg));     str_addz(&dst, "/yazi.toml");
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    str_reset(&src); str_reset(&dst);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/yazi/package.toml");
    str_addz(&dst, str_text(&cfg));     str_addz(&dst, "/package.toml");
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    /* The flavor itself (theme-owned, §6b). yazi wants a DIRECTORY per flavor --
     * flavor.toml plus the tmtheme.xml that colors file previews -- so the theme
     * renders one named after itself. */
    if (*osr_mod_theme() != '\0') {
        Str fl;
        str_init(&fl);
        str_addz(&fl, str_text(&cfg)); str_addz(&fl, "/flavors/");
        str_addz(&fl, osr_mod_theme()); str_addz(&fl, ".yazi");
        (void)osr_mkdir_p(str_text(&fl));
        str_reset(&dst); str_addz(&dst, str_text(&fl)); str_addz(&dst, "/flavor.toml");
        (void)osr_install_theme_layer("yazi", "flavor.toml", str_text(&dst));
        str_reset(&dst); str_addz(&dst, str_text(&fl)); str_addz(&dst, "/tmtheme.xml");
        (void)osr_install_theme_layer("yazi", "tmtheme.xml", str_text(&dst));
        str_free(&fl);
    }

    /* ...and the one-line file that selects it. */
    str_reset(&dst); str_addz(&dst, str_text(&cfg)); str_addz(&dst, "/theme.toml");
    if (!osr_install_theme_layer("yazi", "theme.toml", str_text(&dst))) {
        str_reset(&src);
        str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/yazi/theme.toml");
        if (file_exists(str_text(&src)))
            ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    }
    str_free(&src); str_free(&dst); str_free(&cfg);
    return ok;
}
