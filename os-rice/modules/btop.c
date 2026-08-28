/* modules/btop.c -- btop resource monitor + dotfiles config. ONE copy, POSIX,
 * distro-agnostic (was linux-debian/modules/btop.sh). Native on every target
 * except Debian 11 (bullseye), which gets the upstream static binary via a facet
 * row in apt.map. Config is split by ownership (§5), same shape as foot/ghostty:
 *
 * btop.conf    dotfiles-owned (10-layer) — overwritten on update
 * btop.theme   rice-owned palette (90-layer) — swapped on rice switch (§6),
 * falling back to the dotfiles default when a rice ships none
 *
 * btop.conf carries `color_theme = "rice"`, which btop resolves to the theme
 * named rice.theme in ~/.config/btop/themes — so the palette layer swaps
 * independently of the base config.
 * Palette (rice-owned theme, swapped on switch §6). Rice override wins; the
 * dotfiles default covers a rice that ships no palette. In --module mode
 * OSR_THEME_DIR is whatever rice the theme picker resolved (§6).
 *
 * Port of modules/btop.sh, kept as the reference at
 * test/ref/btop_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_btop(void) {
    static const char *const pkgs[] = { "btop", NULL };
    Str src, dst;
    int ok;

    ok = osr_pkg_install_step("Installing btop", pkgs);

    str_init(&src); str_init(&dst);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/btop/btop.conf");
    str_addz(&dst, osr_mod_home());     str_addz(&dst, "/.config/btop/btop.conf");
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    /* One theme file under a fixed name, so btop.conf can point at it without
     * knowing which theme is current. The theme's own version wins; the
     * dotfiles one is the fallback for a theme that ships none. */
    str_reset(&dst);
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.config/btop/themes/rice.theme");
    if (!osr_install_theme_layer("btop", "btop.theme", str_text(&dst))) {
        str_reset(&src);
        str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/btop/btop.theme");
        if (file_exists(str_text(&src)))
            ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    }
    str_free(&src); str_free(&dst);
    return ok;
}
