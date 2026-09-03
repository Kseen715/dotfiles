/* modules/oh-my-posh.c -- oh-my-posh: package, the Nerd Font its glyphs need,
 * and its theme-owned prompt definition.
 *
 * IT IS NOT THE ACTIVE PROMPT. Starship is (modules/starship.c, the same
 * module `osr module starship` runs on the other side), and this module runs
 * it: on Windows the two are installed together because the pwsh profile wires
 * the prompt up with `starship init powershell`, and oh-my-posh is kept around
 * as a switch-back-able fallback with its own theme file still painted. See
 * the commented init line in PowerShell7-profile. Install-Starship only ever
 * sat inside oh-my-posh.ps1 because that tree had no starship module of its
 * own; everything Starship needs lives in Starship's file now, and this is one
 * call into it rather than a copy of it.
 *
 * PSReadLine's ListView prediction dropdown is untouched by any of this --
 * that is history/readline config, not prompt-engine config.
 *
 * THE THEME FILE IS LITERAL-ONLY, and falls back to the 'osr-rice' theme's
 * copy when the requested theme ships none of its own -- the same fallback
 * Install-OhMyPosh had, and for the same reason: osr-rice is the only prompt
 * anyone has actually defined, so a theme without one wants that rather than
 * no prompt at all.
 *
 * WHERE IT LANDS is oh-my-posh's own themes directory, which only oh-my-posh
 * can name: $POSH_THEMES_PATH if the installer set it, else scoop's prefix for
 * the package, else the directory the binary itself sits in. Guessing it wrong
 * writes a file nothing reads.
 *
 * C89.
 */
#include "../lib/module.h"
#include "../lib/fonts.h"
#include "../lib/render.h"

#include <stddef.h>
#include <string.h>

#ifdef _WIN32

int osrm_starship(void);

static int nerd_font(void *ctx) { return osr_install_nerd_font((const char *)ctx); }

/* posh_themes_dir -- oh-my-posh's own themes directory, asked in the three
 * places it can be, most authoritative first. Empty when none of them names a
 * directory that exists. */
static void posh_themes_dir(Str *out) {
    const char *env = env_str("POSH_THEMES_PATH", "");

    str_reset(out);
    if (*env != '\0' && dir_exists(env)) {
        str_addz(out, env);
        return;
    }

    /* scoop knows where it put the package, and its layout is not something to
     * reconstruct from the outside. */
    if (osr_have_cmd("scoop")) {
        Str prefix;
        char *argv[4];
        str_init(&prefix);
        argv[0] = (char *)"scoop"; argv[1] = (char *)"prefix";
        argv[2] = (char *)"oh-my-posh"; argv[3] = NULL;
        if (osr_run_capture(argv, &prefix)) {
            str_trim_trailing(&prefix, '\n');
            str_trim_trailing(&prefix, '\r');
            if (prefix.len > 0) {
                str_addz(out, str_text(&prefix));
                str_addz(out, "/themes");
            }
        }
        str_free(&prefix);
        if (out->len > 0 && dir_exists(str_text(out))) { str_free(&prefix); return; }
        str_reset(out);
    }

    /* Last resort: beside the binary, which is where a winget or manual
     * install leaves them. */
    {
        Str exe;
        char dir[OSR_PATH_MAX];
        str_init(&exe);
        if (osr_path_lookup("oh-my-posh", &exe)) {
            osr_dirname(str_text(&exe), dir, sizeof(dir));
            str_addz(out, dir);
            str_addz(out, "/themes");
        }
        str_free(&exe);
    }
    if (out->len > 0 && !dir_exists(str_text(out))) str_reset(out);
}

int osrm_oh_my_posh(void) {
    static const char *const pkgs[] = { "oh-my-posh", NULL };
    static const char *const theme_file = "M365Princess++.omp.json";
    Str themes_dir, dst, src;
    int is_temp = 0;
    int ok;

    ok = osr_pkg_install_step("Installing oh-my-posh", pkgs);
    ok = osr_step("Installing JetBrains Mono Nerd Font", nerd_font,
                  (void *)"JetBrainsMono") && ok;

    /* The engine the prompt actually runs on. */
    if (!osrm_starship()) ok = 0;

    str_init(&themes_dir);
    posh_themes_dir(&themes_dir);
    if (themes_dir.len == 0) {
        osr_warnf("oh-my-posh: could not resolve its themes directory; is oh-my-posh installed?");
        str_free(&themes_dir);
        return 0;
    }

    str_init(&dst);
    str_addz(&dst, str_text(&themes_dir));
    str_addc(&dst, '/');
    str_addz(&dst, theme_file);

    str_init(&src);
    if (osr_theme_source(&src, "oh-my-posh", theme_file, &is_temp)) {
        ok = osr_install_file(str_text(&src), str_text(&dst)) && ok;
        if (is_temp) remove(str_text(&src));
        osr_successf("oh-my-posh: themed as '%s' -> %s", osr_mod_theme(), str_text(&dst));
    } else {
        /* osr-rice is the only prompt anyone has defined, so a theme without
         * one gets it rather than nothing. */
        Str fallback;
        str_init(&fallback);
        str_addz(&fallback, osr_mod_root());
        str_addz(&fallback, "/themes/osr-rice/config/oh-my-posh/");
        str_addz(&fallback, theme_file);
        if (file_exists(str_text(&fallback))) {
            osr_warnf("oh-my-posh: theme '%s' ships no prompt; using 'osr-rice', "
                      "the only one defined so far", osr_mod_theme());
            ok = osr_install_file(str_text(&fallback), str_text(&dst)) && ok;
        } else {
            osr_warnf("oh-my-posh: no prompt to install -- theme '%s' ships none and "
                      "themes/osr-rice/config/oh-my-posh/ has none either", osr_mod_theme());
            ok = 0;
        }
        str_free(&fallback);
    }

    str_free(&src);
    str_free(&dst);
    str_free(&themes_dir);
    return ok;
}

#else /* !_WIN32 */

/* oh-my-posh runs on Linux too, but nothing here uses it: the zsh prompt is
 * Starship's, wired up by the theme-owned 90-theme.zsh. Installing a second
 * prompt engine nobody initializes would be a package with no effect. */
int osrm_oh_my_posh(void) { return 0; }

#endif /* _WIN32 */
