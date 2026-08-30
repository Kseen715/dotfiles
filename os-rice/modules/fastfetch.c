/* modules/fastfetch.c -- fastfetch: install the package, then paint the one
 * config.jsonc it reads. The same module on both operating systems, so it is
 * one file, not one file per OS: the two implementations sit side by side
 * below and the compiler picks the branch it is building for.
 *
 * That is the layout rule for every module here -- modules/<name>.c, never
 * modules/<os>/<name>.c. A module that only one OS can have (win-tweaks,
 * flameshot) is simply a file whose other branch is empty; a module both can
 * have is a file with both branches filled in, like this one. Keeping them
 * in one file is what makes "does the Windows side still do what the Linux
 * side does" a question you answer by scrolling, not by diffing two trees.
 *
 * Both branches export the same entry point, osrm_fastfetch, because they are
 * never compiled together: each core calls it with its own tier's contract,
 * and nob.c hands this file to whichever core the host is building (see the
 * comment on lib_srcs/posix_srcs there).
 *
 *   Windows   osrm_fastfetch(repo_root, themes_root, map_path, theme,
 *             theme_only), one row in modules.c's dispatch table. Port of
 *             windows-rice/modules/fastfetch.ps1.
 *   POSIX     osrm_fastfetch(void), one row in lib/modules.c's registry.
 *             Was modules/fastfetch.sh; the config layering it does is
 *             stated in test/unit_c/modules_test.c.
 *
 * What they have in common is the reason the module is short on both: the
 * package is whatever the platform's map resolves `fastfetch` to (a bare
 * passthrough on arch/fedora/void/alpine/gentoo, the official prebuilt .deb
 * on Debian/Ubuntu where it is packaged only on very recent releases, scoop's
 * package on Windows), and fastfetch reads exactly ONE config file, so the
 * theme owns the whole installed file -- it is nothing but presentation
 * (§5). Where the two differ is only the fallback when the current theme
 * ships no version of it: Windows leaves fastfetch's own built-in default and
 * warns, matching Install-Fastfetch; POSIX installs the dotfiles base file,
 * matching install_layer in the .sh.
 *
 * https://github.com/fastfetch-cli/fastfetch
 *
 * C89.
 */
#ifdef _WIN32

#include "src/common.h"

#include "../lib/winpkg.h"
#include "../lib/theme_render.h"
#include "../lib/config_copy.h"
#include "../lib/winui.h"

#include <stddef.h>

int osrm_fastfetch(const char *repo_root, const char *themes_root, const char *map_path,
                   const char *theme, int theme_only) {
    char dest[600];
    char layer_src[700];
    int is_temp;

    if (!theme_only) osr_winpkg_install(map_path, "fastfetch", NULL);

    osr_expand_home("~/.config/fastfetch/config.jsonc", dest, sizeof(dest));
    if (osr_theme_layer_source(themes_root, repo_root, "fastfetch", "config.jsonc", theme,
                                layer_src, sizeof(layer_src), &is_temp)) {
        int ok = osr_copy_file(layer_src, dest);
        osr_theme_layer_cleanup(layer_src, is_temp);
        if (ok) { osr_success("fastfetch: config.jsonc themed as '%s'", theme); return 1; }
        osr_warn("fastfetch: could not write %s", dest);
        return 0;
    }
    osr_warn("fastfetch: no config.jsonc.tmpl or theme '%s'; leaving fastfetch's own default", theme);
    return 1; /* matches Install-Fastfetch: a missing theme layer warns, not fails */
}

#else /* !_WIN32 */

#include "../lib/module.h"

#include <stddef.h>

int osrm_fastfetch(void) {
    static const char *const pkgs[] = { "fastfetch", NULL };
    Str dst;
    Str fallback;
    int ok;

    ok = osr_pkg_install_step("Installing fastfetch", pkgs);

    str_init(&dst);
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.config/fastfetch/config.jsonc");

    if (!osr_install_theme_layer("fastfetch", "config.jsonc", str_text(&dst))) {
        /* No theme version of it: the dotfiles base is the fallback. */
        str_init(&fallback);
        str_addz(&fallback, osr_mod_dotfiles());
        str_addz(&fallback, "/fastfetch/config.jsonc");
        if (file_exists(str_text(&fallback))) {
            osr_install_layer(str_text(&fallback), str_text(&dst));
        }
        str_free(&fallback);
    }
    str_free(&dst);
    return ok;
}

#endif /* _WIN32 */
