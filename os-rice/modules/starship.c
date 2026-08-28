/* modules/starship.c -- Starship prompt + Nerd Font glyphs + rice-owned theme.
 * ONE copy, POSIX, distro-agnostic. Split out of zsh.sh so `osr module starship`
 * installs the prompt, the icons it renders, AND a rice's starship.toml theme in
 * one shot (G5: starship.toml is config, not program data).
 *
 * package        native where available, script: fallback on Fedora/old Ubuntu
 * (see any.map / dnf.map / apt.map)
 * Nerd Font      the glyphs the prompt's icons need (shared lib/fonts.sh)
 * starship.toml  the SHARED dotfiles base (starship/starship.toml) with only the
 * color palette swapped per rice. Composed, not layered, because
 * starship.toml has no include: base body + the rice's
 * starship.palette.toml [palettes.theme] table (§5/§6). A rice
 * that ships no palette gets the base's own default palette.
 *
 * The prompt is wired into the shell by zsh's rice-owned 90-theme.zsh
 * (`eval "$(starship init zsh)"`), so manifest order lists starship before zsh.
 * Compose base + rice palette (§6). Standalone `osr module starship` composes the
 * palette of whichever rice was picked (--theme / interactive / default).
 *
 * Port of modules/starship.sh, kept as the reference at
 * test/ref/starship_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/render.h"
#include "../lib/nerdfont.h"

#include <stddef.h>
#include <unistd.h>

static int nerd_font(void *ctx) { return osr_install_nerd_font((const char *)ctx); }

int osrm_starship(void) {
    static const char *const pkgs[] = { "starship", NULL };
    Str base, dst, pal;
    int is_temp = 0;
    int ok;

    ok = osr_pkg_install_step("Installing Starship prompt", pkgs);
    ok = osr_step("Installing JetBrains Mono Nerd Font", nerd_font,
                  (void *)"JetBrainsMono") && ok;

    str_init(&base); str_init(&dst); str_init(&pal);
    str_addz(&base, osr_mod_dotfiles()); str_addz(&base, "/starship/starship.toml");
    str_addz(&dst, osr_mod_home());      str_addz(&dst, "/.config/starship.toml");
    if (file_exists(str_text(&base))) {
        if (osr_theme_source(&pal, "starship", "starship.palette.toml", &is_temp)) {
            /* One file, two owners: the prompt layout is the rice's and the
             * [palettes] table is the theme's, so they are composed rather than
             * either one installed over the other. */
            ok = osr_compose_starship_config(str_text(&base), str_text(&pal),
                                             str_text(&dst)) && ok;
            if (is_temp) (void)unlink(str_text(&pal));
        } else {
            /* No rice palette -> install the base as-is (its default one). */
            ok = osr_install_layer(str_text(&base), str_text(&dst)) && ok;
        }
    }
    str_free(&base); str_free(&dst); str_free(&pal);
    return ok;
}
