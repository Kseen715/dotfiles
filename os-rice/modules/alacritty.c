/* modules/alacritty.c -- Alacritty terminal + JetBrains Mono Nerd Font + layered
 * config. ONE copy, POSIX, distro-agnostic. Alacritty is native on every target
 * (Debian/Ubuntu, Fedora, Arch, Void, Alpine, Gentoo), so the logical name passes
 * through pkgmap unchanged - no source: row, no build.
 *
 * WHY IT IS HERE. It is the last actively-developed terminal that starts on
 * pre-2011 Intel graphics: Ghostty requires OpenGL 4.3 (since 1.2) and kitty 3.3,
 * while an Ironlake-era iGPU stops at desktop GL 2.1 and both die with "unable to
 * acquire an OpenGL context for rendering". Alacritty carries a GLES2 fallback
 * renderer and selects it automatically on that hardware - no env vars, and much
 * faster than driving a GPU terminal through llvmpipe. On a modern GPU it takes
 * the GL 3.3 path and behaves like any other install.
 *
 * Config is split by ownership (§5), same shape as foot/ghostty:
 *
 * alacritty.toml         dotfiles-owned (10-layer) — overwritten on update;
 * behaviour only, and the TERM=xterm-256color choice
 * that keeps ssh from breaking (Alacritty has no
 * ssh-terminfo equivalent)
 * alacritty-theme.toml   rice-owned palette (90-layer) — swapped on rice switch
 * (§6), falling back to the dotfiles default when a rice
 * ships none. Owns the colors AND window.opacity.
 *
 * alacritty.toml carries `import = ["~/.config/alacritty/alacritty-theme.toml"]`,
 * so the palette layer swaps independently of the base — the §5 split applied to
 * a DE config. install_alacritty_config, not install_layer: `import` moved into
 * `[general]` in Alacritty 0.14, so the file is adapted to the Alacritty that was
 * just installed (see lib/config.sh).
 *
 * Alacritty has no image protocol (no kitty graphics, no sixel - both refused
 * upstream), so yazi previews images through chafa. modules/yazi.sh installs it;
 * list `yazi` in the rice and image previews degrade to unicode blocks instead of
 * vanishing (§9).
 *
 * The Nerd Font install is the shared, best-effort lib/fonts.sh helper (also used
 * by foot/ghostty/starship/wezterm) — one copy of the download-unzip-register
 * logic.
 * Base config (dotfiles-owned, overwrite-on-update §5).
 * Palette (rice-owned theme, swapped on switch §6). Rice override wins; the
 * dotfiles default covers a rice that ships no palette. In --module mode
 * OSR_THEME_DIR is whatever rice the theme picker resolved (§6).
 *
 * Was modules/alacritty.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/nerdfont.h"

#include <stddef.h>
#include <stdlib.h>

static int nerd_font(void *ctx) { return osr_install_nerd_font((const char *)ctx); }

int osrm_alacritty(void) {
    static const char *const pkgs[] = { "alacritty", "unzip", "fontconfig", NULL };
    Str src, dst;
    int ok;

    ok = osr_pkg_install_step("Installing Alacritty", pkgs);
    ok = osr_step("Installing JetBrains Mono Nerd Font", nerd_font,
                  (void *)"JetBrainsMono") && ok;

    /* Through install_alacritty_config, not install_layer: alacritty moved
     * several top-level keys under [general] in 0.14, and the adapter is what
     * makes one config file work on both sides of that. */
    str_init(&src); str_init(&dst);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/alacritty/alacritty.toml");
    str_addz(&dst, osr_mod_home());     str_addz(&dst, "/.config/alacritty/alacritty.toml");
    if (file_exists(str_text(&src)))
        ok = osr_install_alacritty_config(str_text(&src), str_text(&dst)) && ok;

    str_reset(&dst);
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.config/alacritty/alacritty-theme.toml");
    if (!osr_install_theme_layer("alacritty", "alacritty-theme.toml", str_text(&dst))) {
        str_reset(&src);
        str_addz(&src, osr_mod_dotfiles());
        str_addz(&src, "/alacritty/alacritty-theme.toml");
        if (file_exists(str_text(&src)))
            ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    }
    str_free(&src); str_free(&dst);
    return ok;
}
