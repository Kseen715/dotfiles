/* modules/starship.c -- the Starship prompt: package, the Nerd Font glyphs its
 * icons need, the ccver helper the prompt runs, and a theme-owned palette.
 *
 * ONE FUNCTION, and the four steps are the same four on both systems. What the
 * two branches are for is one step inside ccver_build -- where a compiled
 * helper goes so that the shell will find it -- because the two systems have
 * different answers to that and only that.
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
 * ccver          starship/ccver.c compiled onto PATH -- the helper the
 *                prompt's [custom.c] module runs to list the C compilers
 *                present. Program data, not config: a binary this module owns
 *                the way it owns the package
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
 * WHY ccver IS A COMPILED HELPER, and the one thing that genuinely differs
 * between the two systems' problems: starship runs a custom module's command
 * through `sh -c` on POSIX but `cmd /C` on Windows. The POSIX one-liner this
 * replaced (`for c in gcc clang tcc; do ... sed ...`) therefore printed
 * nothing on Windows, and the pwsh prompt showed a bare symbol with no version
 * while the identical starship.toml worked on Linux. One binary is the one
 * command string both shells can run -- and it lists EVERY compiler on PATH,
 * which starship's built-in [c] module cannot do (it reports only the first it
 * finds).
 *
 * C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/render.h"
#include "../lib/fonts.h"
#ifdef _WIN32
#include "../lib/build.h"
#endif

#include <stddef.h>

static int nerd_font(void *ctx) { return osr_install_nerd_font((const char *)ctx); }

/* ccver_dir -- where a program this module compiles belongs, so that the
 * shell finds it without anything else being told.
 *
 * POSIX has a convention to reuse: ~/.local/bin is already on PATH through the
 * shell layers (see modules/lcc.c and the 00-env.zsh layer), so nothing else
 * has to happen. Windows has no such convention, so os-rice keeps its own --
 * %LOCALAPPDATA%\osr\bin\<name>, the same place every binary a source: builder
 * installs goes -- and this module is what puts that directory on PATH. */
static int ccver_dir(Str *out) {
#ifdef _WIN32
    char dir[OSR_PATH_MAX];
    if (!osr_bin_dir("ccver", dir, sizeof(dir))) return 0;
    str_addz(out, dir);
    return 1;
#else
    str_addz(out, osr_mod_home());
    str_addz(out, "/.local/bin");
    return 1;
#endif
}

/* ccver_build -- compile the dotfiles' starship/ccver.c onto PATH. See the
 * file header for what ccver is and why it is a binary.
 *
 * Rebuilt on every run: it is one cc invocation, and it keeps the binary in
 * step with edits to ccver.c. A box with no C compiler is not an error -- the
 * prompt just shows no version there. */
static int ccver_build(void) {
    static const char *const ccs[] = { "cc", "gcc", "clang" };
    char *argv[7];
    Str src, bin, dst;
    const char *cc = NULL;
    int i;
    int ok = 1;

    if (osr_theme_only()) return osr_theme_only_skip("ccver_build");

    str_init(&src); str_init(&bin); str_init(&dst);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/starship/ccver.c");
    if (!ccver_dir(&bin)) { ok = 0; goto out; }
    str_addz(&dst, str_text(&bin));
    str_addz(&dst, "/ccver");
#ifdef _WIN32
    str_addz(&dst, ".exe");
#endif

    /* A checkout that predates ccver.c: nothing to build, nothing to warn. */
    if (!file_exists(str_text(&src))) goto out;

    for (i = 0; i < (int)(sizeof ccs / sizeof ccs[0]); i++) {
        if (osr_have_cmd(ccs[i])) { cc = ccs[i]; break; }
    }
    if (cc == NULL) {
        osr_warnf("starship: no C compiler (cc/gcc/clang) - the prompt's C "
                  "module shows no version until ccver is built");
        goto out;
    }

    ok = osr_mkdir_p(str_text(&bin));
    if (ok) {
        argv[0] = (char *)cc;
        argv[1] = (char *)"-O2";
        argv[2] = (char *)"-std=c89";
        argv[3] = (char *)"-o";
        argv[4] = (char *)str_text(&dst);
        argv[5] = (char *)str_text(&src);
        argv[6] = NULL;
        ok = osr_run_step_user("Building ccver (compiler versions for the prompt)",
                               argv);
    }

#ifdef _WIN32
    /* HKCU PATH plus this process, so a rice that just ran gets it too. There
     * is no shell layer here to have put the directory on PATH already. */
    if (ok) {
        osr_add_to_path(str_text(&bin));
        osr_successf("starship: ccver -> %s", str_text(&bin));
    }
#endif

out:
    str_free(&src); str_free(&bin); str_free(&dst);
    return ok;
}

int osrm_starship(void) {
    static const char *const pkgs[] = { "starship", NULL };
    Str base, dst, pal;
    int is_temp = 0;
    int ok;

    ok = osr_pkg_install_step("Installing Starship prompt", pkgs);
    ok = osr_step("Installing JetBrains Mono Nerd Font", nerd_font,
                  (void *)"JetBrainsMono") && ok;
    ok = ccver_build() && ok;

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
