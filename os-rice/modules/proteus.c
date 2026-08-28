/* modules/proteus.c -- Proteus, the theme/wallpaper picker (§6a).
 *
 * The GUI half of `osr theme`: a rofi-style overlay that lists the themes with
 * their wallpapers as previews and applies the one you pick. It is the only
 * module here that is BOTH sessions rather than one, because that is its whole
 * point - X11 (override-redirect) and Wayland (wlr-layer-shell) in one binary.
 *
 * Built from source in this repo (../proteus), not fetched: it is part of the
 * dotfiles, and a picker that reads this repo's themes has no meaning apart
 * from it. The build is dispatched by any.map -> source:provide_proteus, which
 * lives in lib/build.sh alongside the other source: providers. `cargo install
 * --path` puts the binary in $OSR_HOME/.local/bin, which the shell layers
 * already have on PATH.
 *
 * Config split (§5): proteus.toml is dotfiles-owned (geometry, behaviour), and
 * it deliberately carries no colors - Proteus reads the palette out of whichever
 * theme is under the cursor, so there is no rice-owned layer to swap.
 *
 * No package is listed for libxkbcommon or libwayland even though Proteus uses
 * both: they are dlopen'd, not linked, and the only session that needs them
 * (Wayland) cannot exist without them - the compositor itself is a client of
 * both. On X11 neither is touched, and the picker still builds and runs on a
 * machine that has neither.
 *
 * Port of modules/proteus.sh, kept as the reference at
 * test/ref/proteus_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_proteus(void) {
    static const char *const pkgs[] = { "proteus", NULL };
    Str dir, src, dst;
    int ok;

    ok = osr_pkg_install_step("Installing proteus", pkgs);
    str_init(&dir); str_init(&src); str_init(&dst);
    str_addz(&dir, osr_mod_home()); str_addz(&dir, "/.config/proteus");
    ok = osr_mkdir_p(str_text(&dir)) && ok;
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/proteus/proteus.toml");
    str_addz(&dst, str_text(&dir));     str_addz(&dst, "/proteus.toml");
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    str_free(&dir); str_free(&src); str_free(&dst);
    return ok;
}
