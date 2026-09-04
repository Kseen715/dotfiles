/* modules/osrvv.c -- osrvv: the version probe the starship prompt runs.
 *
 * osrvv/osrvv.c in the dotfiles is a standalone C89 program that prints one
 * toolchain's version in the form the prompt renders -- `osrvv c` ->
 * "v15.2.0-gcc v20.1.0-clang". This module is its build: one cc invocation
 * onto PATH, on either system. See osrvv/osrvv.c's header for why the probe is
 * a program and not the `command = "... | sed ..."` one-liners it replaced
 * (starship runs those through `sh -c` on POSIX and `cmd /C` on Windows, so no
 * one string served both and the Windows prompt showed no versions at all).
 *
 * A MODULE OF ITS OWN, and called from modules/starship.c rather than listed
 * before it: starship.toml is what runs osrvv, so a starship install that
 * skipped it would render a prompt with holes in it, which manifest ordering
 * cannot guarantee (§4 orders what a manifest LISTS; nothing makes a manifest
 * list this). `osr module osrvv` rebuilds the binary on its own -- the one
 * thing to run after editing osrvv.c -- and starship calls the same entry
 * point, so the two paths cannot drift.
 *
 * ONE FUNCTION, and the branch inside it is over the one thing the systems
 * genuinely answer differently: where a compiled helper goes so that the shell
 * finds it. POSIX has a convention to reuse -- ~/.local/bin is already on PATH
 * through the zsh layers (zsh/rc.d/00-env.zsh). Windows has none, so os-rice
 * keeps its own, %LOCALAPPDATA%\osr\bin, the same place every binary a
 * `source:` builder installs goes, and this module puts it on PATH.
 *
 * Rebuilt on every run: it is one cc invocation, and it keeps the binary in
 * step with edits to osrvv.c. A box with no C compiler is not an error -- the
 * prompt just shows no versions there, which is what it showed before.
 *
 * C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#ifdef _WIN32
#include "../lib/build.h"
#endif

#include <stddef.h>
#include <stdio.h>

/* osrvv_dir -- the directory a program this module compiles belongs in, so
 * that the prompt's shell finds it without anything else being told. */
static int osrvv_dir(Str *out) {
#ifdef _WIN32
    char dir[OSR_PATH_MAX];
    if (!osr_bin_dir("osrvv", dir, sizeof(dir))) return 0;
    str_addz(out, dir);
    return 1;
#else
    str_addz(out, osr_mod_home());
    str_addz(out, "/.local/bin");
    return 1;
#endif
}

int osrm_osrvv(void) {
    static const char *const ccs[] = { "cc", "gcc", "clang" };
    char *argv[7];
    Str src, bin, dst;
    const char *cc = NULL;
    int i;
    int ok = 1;

    if (osr_theme_only()) return osr_theme_only_skip("osrvv");

    str_init(&src); str_init(&bin); str_init(&dst);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/osrvv/osrvv.c");
    if (!osrvv_dir(&bin)) { ok = 0; goto out; }
    str_addz(&dst, str_text(&bin));
    str_addz(&dst, "/osrvv");
#ifdef _WIN32
    str_addz(&dst, ".exe");
#endif

    /* A checkout that predates osrvv.c: nothing to build, nothing to warn. */
    if (!file_exists(str_text(&src))) goto out;

    for (i = 0; i < (int)(sizeof ccs / sizeof ccs[0]); i++) {
        if (osr_have_cmd(ccs[i])) { cc = ccs[i]; break; }
    }
    if (cc == NULL) {
        osr_warnf("osrvv: no C compiler (cc/gcc/clang) - the prompt shows no "
                  "toolchain versions until osrvv is built");
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
        ok = osr_run_step_user("Building osrvv (toolchain versions for the prompt)",
                               argv);
    }

#ifdef _WIN32
    /* HKCU PATH plus this process, so a rice that just ran gets it too. There
     * is no shell layer here to have put the directory on PATH already. */
    if (ok) {
        osr_add_to_path(str_text(&bin));
        osr_successf("osrvv: %s", str_text(&dst));
    }
#endif

out:
    str_free(&src); str_free(&bin); str_free(&dst);
    return ok;
}
