/* modules/starship.c -- the Starship prompt: package, the Nerd Font glyphs its
 * icons need, the osrvv version probe the prompt runs, and a theme-owned
 * palette.
 *
 * ONE FUNCTION, four steps, the same four on both systems -- the only place
 * the two differ is inside modules/osrvv.c, which owns that difference.
 *
 * On Windows this module has no row of its own in lib/modules.c: the prompt
 * engine is installed as part of the oh-my-posh module, which keeps oh-my-posh
 * around as a switch-back-able fallback and calls in here for the engine that
 * is actually running. On POSIX it is a row like any other, and `osr module
 * starship` installs the prompt, the icons it renders and the theme's palette
 * in one shot (starship.toml is config, not program data).
 *
 * What both install:
 *
 * package        native where available, script: fallback on Fedora and older
 *                Ubuntu (see the maps); winget's on Windows
 * Nerd Font      the glyphs the prompt's icons need (lib/fonts.c)
 * osrvv          osrvv/osrvv.c compiled onto PATH (modules/osrvv.c) -- the
 *                program every [custom.<lang>] module in starship.toml runs to
 *                print a toolchain version. Program data, not config: a binary
 *                this module owns the way it owns the package. It is a
 *                DEPENDENCY, not a manifest neighbour: starship.toml is what
 *                calls osrvv, so a starship installed without it renders a
 *                prompt with holes in it
 * starship.toml  the SHARED dotfiles base (starship/starship.toml) with only
 *                the color palette swapped per theme. COMPOSED, not layered,
 *                because starship.toml has no include directive: base body,
 *                then the theme's starship.palette.toml [palettes.theme]
 *                table (sections 5 and 6). A theme that ships no palette gets
 *                the base's own default one.
 *
 * The prompt is wired into the shell by the theme-owned 90-theme.zsh
 * (`eval "$(starship init zsh)"`) on POSIX and by PowerShell7-profile's
 * profile.ps1 (`starship init powershell`) on Windows, so a manifest lists
 * starship before zsh.
 *
 * C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/render.h"
#include "../lib/fonts.h"

/* modules/osrvv.c: the version probe the prompt runs, built as part of this
 * module rather than merely ordered before it (see that file's header). */
int osrm_osrvv(void);

#include <stddef.h>

static int nerd_font(void *ctx) { return osr_install_nerd_font((const char *)ctx); }

int osrm_starship(void) {
    static const char *const pkgs[] = { "starship", NULL };
    Str base, dst, pal;
    int is_temp = 0;
    int ok;

    ok = osr_pkg_install_step("Installing Starship prompt", pkgs);
    ok = osr_step("Installing JetBrains Mono Nerd Font", nerd_font,
                  (void *)"JetBrainsMono") && ok;
    ok = osrm_osrvv() && ok;

    str_init(&base); str_init(&dst); str_init(&pal);
    str_addz(&base, osr_mod_dotfiles()); str_addz(&base, "/starship/starship.toml");
    str_addz(&dst, osr_mod_home());      str_addz(&dst, "/.config/starship.toml");
    if (file_exists(str_text(&base))) {
        if (osr_theme_source(&pal, "starship", "starship.palette.toml", &is_temp)) {
            /* One file, two owners: the prompt layout is the dotfiles' and the
             * [palettes] table is the theme's, so they are composed rather
             * than either one installed over the other. */
            ok = osr_compose_starship_config(str_text(&base), str_text(&pal),
                                             str_text(&dst)) && ok;
            if (is_temp) remove(str_text(&pal));
        } else {
            /* No theme palette -> install the base as-is (its default one). */
            ok = osr_install_layer(str_text(&base), str_text(&dst)) && ok;
        }
    }
    str_free(&base); str_free(&dst); str_free(&pal);
    return ok;
}
