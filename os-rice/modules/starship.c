/* modules/starship.c -- Starship prompt + Nerd Font glyphs + the ccver helper
 * + a rice-owned theme. The same module on both operating systems, so it is
 * one file, not one file per OS: the two implementations sit side by side
 * below and the compiler picks the branch it is building for (the layout rule
 * stated in modules/fastfetch.c and on nob.c's lib_srcs/posix_srcs).
 *
 * Both branches export the same entry point, osrm_starship, because they are
 * never compiled together:
 *
 *   Windows   osrm_starship(repo_root, themes_root, map_path, theme,
 *             theme_only), called by modules/oh-my-posh.c -- on Windows the
 *             prompt engine is installed as part of the oh-my-posh module
 *             (which keeps oh-my-posh itself around as a switch-back-able
 *             fallback), so this branch has no row of its own in modules.c's
 *             dispatch table. Was Install-Starship inside oh-my-posh.ps1.
 *   POSIX     osrm_starship(void), one row in lib/modules.c's registry. Split
 *             out of zsh.sh so `osr module starship` installs the prompt, the
 *             icons it renders, AND a rice's starship.toml theme in one shot
 *             (G5: starship.toml is config, not program data). Was
 *             modules/starship.sh; what it must do is stated in the C tests
 *             under test/unit_c/ rather than diffed against a recording.
 *
 * What both branches install:
 *
 * package        native where available, script: fallback on Fedora/old Ubuntu
 * (see any.map / dnf.map / apt.map); scoop's on Windows
 * Nerd Font      the glyphs the prompt's icons need (shared lib/fonts.sh)
 * ccver          starship/ccver.c compiled onto PATH -- the helper the prompt's
 * [custom.c] module runs to list the C compilers on PATH.
 * Program data, not config: a binary this module owns the way
 * the package above is. ~/.local/bin on POSIX (already on the
 * shell layers' PATH), %LOCALAPPDATA%\osr\bin\ccver on Windows
 * (lib/winbin.h's convention, put on PATH from here)
 * starship.toml  the SHARED dotfiles base (starship/starship.toml) with only the
 * color palette swapped per rice. Composed, not layered, because
 * starship.toml has no include: base body + the rice's
 * starship.palette.toml [palettes.theme] table (§5/§6). A rice
 * that ships no palette gets the base's own default palette.
 *
 * The prompt is wired into the shell by zsh's rice-owned 90-theme.zsh
 * (`eval "$(starship init zsh)"`) on POSIX and by PowerShell7-profile's
 * profile.ps1 (`starship init powershell`) on Windows, so manifest order lists
 * starship before zsh.
 *
 * WHY ccver IS A COMPILED HELPER, and the one thing that genuinely differs
 * between the two branches' problems: starship runs a custom module's command
 * through `sh -c` on POSIX but `cmd /C` on Windows. The POSIX one-liner this
 * replaced (`for c in gcc clang tcc; do ... sed ...`) therefore printed
 * nothing on Windows, and the pwsh prompt showed a bare symbol with no
 * version while the identical starship.toml worked on Linux. One binary is
 * the one command string both shells can run -- and it lists EVERY compiler
 * on PATH, which starship's built-in [c] module cannot do (it reports only
 * the first one it finds).
 *
 * C89.
 */
#ifdef _WIN32

#include "src/common.h"

#include "../lib/winpkg.h"
#include "../lib/winbin.h"
#include "../lib/fonts.h"
#include "../lib/theme_render.h"
#include "../lib/config_copy.h"
#include "../lib/winui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* compose_starship_toml -- C port of lib/config.sh's compose_starship_config:
 * the shared base (its own trailing `[palettes.theme]` table, which starts
 * the first line matching that header, stripped) followed by the rice's
 * palette fragment. starship.toml has no include mechanism, so composition
 * is how the base/theme split (§5/§6) is realized for it -- same reason the
 * sh side does it this way. Returns 1 on success.
 */
static int compose_starship_toml(const char *base_path, const char *frag_path, const char *dest_path) {
    FILE *bfp, *ffp, *ofp;
    long bsize;
    char *btext;
    char *cut;
    char fragbuf[8192];
    size_t n;
    int ok = 1;

    bfp = fopen(base_path, "rb");
    if (bfp == NULL) return 0;
    fseek(bfp, 0, SEEK_END);
    bsize = ftell(bfp);
    fseek(bfp, 0, SEEK_SET);
    if (bsize < 0) { fclose(bfp); return 0; }
    btext = (char *)malloc((size_t)bsize + 1);
    if (btext == NULL) { fclose(bfp); return 0; }
    if (bsize > 0 && fread(btext, 1, (size_t)bsize, bfp) != (size_t)bsize) {
        fclose(bfp); free(btext); return 0;
    }
    btext[bsize] = '\0';
    fclose(bfp);

    cut = strstr(btext, "\n[palettes.theme]");
    if (cut != NULL) cut[1] = '\0'; /* keep the newline before it, drop the rest */

    ofp = fopen(dest_path, "wb");
    if (ofp == NULL) { free(btext); return 0; }
    if (fwrite(btext, 1, strlen(btext), ofp) != strlen(btext)) ok = 0;
    free(btext);

    ffp = fopen(frag_path, "rb");
    if (ffp == NULL) { fclose(ofp); return 0; }
    while ((n = fread(fragbuf, 1, sizeof(fragbuf), ffp)) > 0) {
        if (fwrite(fragbuf, 1, n, ofp) != n) { ok = 0; break; }
    }
    fclose(ffp);
    fclose(ofp);
    return ok;
}

/* cat -- bounded append; returns 0 when s would not fit. The one string
 * operation this branch needs that modules/src/common.h does not provide. */
static int cat(char *dst, unsigned long dst_sz, const char *s) {
    unsigned long len = (unsigned long)strlen(dst);
    unsigned long add = (unsigned long)strlen(s);

    if (len + add + 1 > dst_sz) return 0;
    memcpy(dst + len, s, add + 1);
    return 1;
}

/* install_ccver -- build starship/ccver.c and put it on PATH, the Windows half
 * of the POSIX branch's ccver_build. The binary lives where every other
 * os-rice-installed executable does (%LOCALAPPDATA%\osr\bin\ccver, see
 * lib/winbin.h), compiled in the matching src\ directory -- there is no
 * ~/.local/bin convention here to reuse, so this branch also owns putting that
 * directory on PATH.
 *
 * Rebuilt on every run, like the POSIX branch: it is one gcc invocation, and
 * it keeps the binary in step with edits to ccver.c.
 *
 * Never fatal, like the package and font installs beside it: a box with no C
 * compiler just gets no C version in its prompt.
 */
static int install_ccver(const char *repo_root) {
    static const char *const ccs[] = { "gcc", "clang", "cc" };
    char src[700];
    char srcdir[700];
    char bindir[700];
    char staged[700];
    char built[700];
    char cmd[900];
    const char *cc = NULL;
    int i;

    osrm_path_join(src, sizeof(src), repo_root, "starship");
    osrm_path_join(src, sizeof(src), src, "ccver.c");
    /* A checkout that predates ccver.c: nothing to build, nothing to warn. */
    if (!osr_winbin_file_exists(src)) return 1;

    for (i = 0; i < (int)(sizeof ccs / sizeof ccs[0]); i++) {
        if (osr_winpkg_have_command(ccs[i])) { cc = ccs[i]; break; }
    }
    if (cc == NULL) {
        osr_warn("starship: no C compiler (gcc/clang/cc) - the prompt's C "
                 "module shows no version until ccver is built");
        return 1;
    }

    if (!osr_winbin_src_dir("ccver", srcdir, sizeof(srcdir)) ||
        !osr_winbin_bin_dir("ccver", bindir, sizeof(bindir))) return 0;
    /* place() copies through osr_copy_file, which creates the tree -- so this
     * is also what makes srcdir exist for the compiler to write into. */
    if (!osr_winbin_place(src, srcdir, "ccver.c")) return 0;

    osrm_path_join(staged, sizeof(staged), srcdir, "ccver.c");
    osrm_path_join(built, sizeof(built), srcdir, "ccver.exe");

    cmd[0] = '\0';
    if (!cat(cmd, sizeof(cmd), "\"") ||
        !cat(cmd, sizeof(cmd), cc) ||
        !cat(cmd, sizeof(cmd), "\" -O2 -std=c89 -o \"") ||
        !cat(cmd, sizeof(cmd), built) ||
        !cat(cmd, sizeof(cmd), "\" \"") ||
        !cat(cmd, sizeof(cmd), staged) ||
        !cat(cmd, sizeof(cmd), "\"")) return 0;

    if (osr_run_step("Building ccver (compiler versions for the prompt)", cmd) != 0) {
        osr_warn("starship: %s could not build %s", cc, staged);
        return 0;
    }
    if (!osr_winbin_place(built, bindir, "ccver.exe")) return 0;

    /* HKCU PATH plus this process, so a rice that just ran gets it too. */
    osr_winbin_add_to_path(bindir);
    osr_success("starship: ccver -> %s", bindir);
    return 1;
}

int osrm_starship(const char *repo_root, const char *themes_root, const char *map_path,
                  const char *theme, int theme_only) {
    char base[700];
    char dest[700];
    char layer_src[700];
    int is_temp;
    int ok;

    if (!theme_only) {
        osr_winpkg_install(map_path, "starship", NULL);
        osr_install_nerd_font("JetBrainsMono");
        install_ccver(repo_root);
    }

    osrm_path_join(base, sizeof(base), repo_root, "starship");
    osrm_path_join(base, sizeof(base), base, "starship.toml");
    osr_expand_home("~/.config/starship.toml", dest, sizeof(dest));

    if (osr_theme_layer_source(themes_root, repo_root, "starship", "starship.palette.toml",
                                theme, layer_src, sizeof(layer_src), &is_temp)) {
        ok = compose_starship_toml(base, layer_src, dest);
        osr_theme_layer_cleanup(layer_src, is_temp);
    } else {
        /* No rice palette resolves -> install the base as-is, same fallback
         * the POSIX branch uses for a rice that ships none. */
        ok = osr_copy_file(base, dest);
    }

    if (ok) osr_success("starship: themed as '%s' -> %s", theme, dest);
    else osr_warn("starship: could not write %s", dest);
    return ok;
}

#else /* !_WIN32 */

#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/render.h"
#include "../lib/nerdfont.h"

#include <stddef.h>
#include <unistd.h>

static int nerd_font(void *ctx) { return osr_install_nerd_font((const char *)ctx); }

/* ccver_build -- compile the dotfiles' starship/ccver.c into ~/.local/bin,
 * which the shell layers already put on PATH (see modules/lcc.c, the 00-env.zsh
 * layer). See this file's header for what ccver is and why it is a binary.
 *
 * Rebuilt on every run: it is one cc invocation, and it keeps the binary in
 * step with edits to ccver.c. A box with no C compiler is not an error, the
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
    str_addz(&bin, osr_mod_home());     str_addz(&bin, "/.local/bin");
    str_addz(&dst, str_text(&bin));     str_addz(&dst, "/ccver");

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

#endif /* _WIN32 */
