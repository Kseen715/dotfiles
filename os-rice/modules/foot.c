/* modules/foot.c -- foot terminal + JetBrains Mono Nerd Font + layered config.
 * ONE copy, POSIX, distro-agnostic (was linux-rhel/modules/foot.sh, bash). The
 * package goes through pkg_install/pkgmap; the font is a best-effort cosmetic
 * asset (warn, never fail a run); config is split by ownership (§5):
 *
 * foot.ini          dotfiles-owned (10-layer) — overwritten on update
 * foot-colors.ini   rice-owned theme (90-layer) — swapped on rice switch (§6),
 * falling back to the dotfiles default when a rice ships none
 *
 * foot.ini carries `include=~/.config/foot/foot-colors.ini`, so the palette layer
 * swaps independently of the base config — the §5 split applied to a DE config.
 *
 * The Nerd Font install is the shared, best-effort lib/fonts.sh helper (also used
 * by starship/wezterm) — one copy of the download-unzip-register logic.
 * Base config (dotfiles-owned, overwrite-on-update §5).
 * Palette (rice-owned theme, swapped on switch §6). Rice override wins; the
 * dotfiles default covers a rice that ships no palette. install_foot_palette,
 * not install_layer: the palette section was renamed in foot 1.26, so the file
 * is adapted to the foot that was just installed.
 *
 * Port of modules/foot.sh, kept as the reference at
 * test/ref/foot_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/render.h"
#include "../lib/nerdfont.h"

#include <stddef.h>
#include <unistd.h>

static int nerd_font(void *ctx) { return osr_install_nerd_font((const char *)ctx); }

int osrm_foot(void) {
    static const char *const pkgs[] = { "foot", "unzip", "fontconfig", NULL };
    Str src, dst, pal;
    int is_temp = 0;
    int ok;

    ok = osr_pkg_install_step("Installing foot terminal", pkgs);
    ok = osr_step("Installing JetBrains Mono Nerd Font", nerd_font,
                  (void *)"JetBrainsMono") && ok;

    str_init(&src); str_init(&dst); str_init(&pal);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/foot/foot.ini");
    str_addz(&dst, osr_mod_home());     str_addz(&dst, "/.config/foot/foot.ini");
    ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    /* The palette goes through install_foot_palette, not install_layer: foot
     * renamed its colour sections between releases, and the adapter is what
     * makes one theme file work on both. */
    str_reset(&dst);
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.config/foot/foot-colors.ini");
    if (osr_theme_source(&pal, "foot", "foot-colors.ini", &is_temp)) {
        ok = osr_install_foot_palette(str_text(&pal), str_text(&dst)) && ok;
        if (is_temp) (void)unlink(str_text(&pal));
    } else {
        str_reset(&src);
        str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/foot/foot-colors.ini");
        if (file_exists(str_text(&src)))
            ok = osr_install_foot_palette(str_text(&src), str_text(&dst)) && ok;
    }
    str_free(&src); str_free(&dst); str_free(&pal);
    return ok;
}
