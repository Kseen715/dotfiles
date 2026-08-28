/* modules/serie.c -- serie, a rich git-commit-graph TUI. ONE copy, POSIX,
 * distro-agnostic (was linux-debian/modules/serie.sh). Native on arch/alpine;
 * everywhere else it is installed from crates.io via the cargo: provider, so the
 * Rust toolchain is a prerequisite and is installed here first (manifest order
 * is the dependency graph, §4).
 * Ensure a toolchain exists before any cargo: resolution (no-op if serie is
 * native on this distro, but harmless — rust is idempotent).
 * config.toml is a pure palette here (§5): serie has no include mechanism and we
 * ship no non-color settings, so the whole file is the rice-owned theme layer
 * (90-*, swapped on rice switch, §6). Rice override wins; the dotfiles default
 * covers a rice that ships none. In --module mode OSR_THEME_DIR is whatever rice
 * the theme picker resolved (§6).
 *
 * Port of modules/serie.sh, kept as the reference at
 * test/ref/serie_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int osrm_rust(void);

int osrm_serie(void) {
    static const char *const pkgs[] = { "serie", NULL };
    Str rhs, src, dst;
    int ok;

    /* serie is packaged almost nowhere, so on most targets it is a `cargo:`
     * row - and a cargo row needs a toolchain that nothing else in a minimal
     * rice installs. Pulling `rust` in HERE rather than listing it in every
     * rice keeps the dependency where the reason for it is. */
    str_init(&rhs);
    osr_pkgmap_resolve(&rhs, "serie");
    if (strncmp(str_text(&rhs), "cargo:", 6) == 0) (void)osrm_rust();
    str_free(&rhs);

    ok = osr_pkg_install_step("Installing serie", pkgs);

    str_init(&src); str_init(&dst);
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.config/serie/config.toml");
    if (!osr_install_theme_layer("serie", "config.toml", str_text(&dst))) {
        str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/serie/config.toml");
        if (file_exists(str_text(&src)))
            ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    }
    str_free(&src); str_free(&dst);
    return ok;
}
