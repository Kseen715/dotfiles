/* modules/linux/fastfetch.c -- fastfetch system info tool + layered config.
 * "Easiest method per distro" is expressed entirely in the pkgmap: native
 * package on arch/fedora/void/alpine/gentoo (bare passthrough), and the
 * official prebuilt .deb on Debian/Ubuntu (apt.map -> provide_fastfetch_deb),
 * where fastfetch is packaged natively only on very recent releases.
 *
 * Config split (§5): fastfetch reads exactly one config.jsonc, so the rice
 * owns the whole installed file (it is nothing but presentation) while the
 * dotfiles base is the fallback for a rice that ships none.
 *
 * Port of modules/fastfetch.sh, kept as the reference at
 * test/ref/fastfetch_sh_ref.sh -- not to be confused with modules/fastfetch.c,
 * which is the WINDOWS core's fastfetch module.
 *
 * https://github.com/fastfetch-cli/fastfetch
 *
 * C89.
 */
#include "../../lib/module.h"

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
