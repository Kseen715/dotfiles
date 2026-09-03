/* lib/ccsrc.c -- the implementation of lib/ccsrc.h.
 *
 * The whole file is string building around six steps, in this order:
 * probe, dependencies, checkout, build, hello world, symlink. Each mutating
 * step is one `sh -c` under osr_run_step_user, the same way modules/lcc.c
 * runs its recipe: the commands are long enough (a configure line, a make
 * with four variables) that spelling them as argv vectors would hide what
 * is being run rather than reveal it.
 *
 * The probe is first and is the reason a rerun costs nothing (SS2): a
 * driver already at <prefix>/bin/<name> means no package manager runs, no
 * network is touched and no compiler starts.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "ccsrc.h"
#include "module.h"

#include <stddef.h>
#include <sys/stat.h>

/* is_exec -- 1 when path exists and its owner may execute it. */
static int is_exec(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IXUSR) != 0;
}

/* run_sh_user -- one osr_run_step_user around `sh -c <script>`. */
static int run_sh_user(const char *desc, const char *script) {
    char *argv[4];
    argv[0] = (char *)"sh"; argv[1] = (char *)"-c";
    argv[2] = (char *)script; argv[3] = NULL;
    return osr_run_step_user(desc, argv);
}

/* say -- "<what> <name>" into out, for the step descriptions. */
static void say(Str *out, const char *what, const char *name) {
    str_reset(out);
    str_addz(out, what);
    str_addc(out, ' ');
    str_addz(out, name);
}

/* quoted -- append "<text>" (double quotes included) to a script under
 * construction. Home directories with a double quote in them are not a case
 * this repo supports anywhere, so the text is not escaped further. */
static void quoted(Str *s, const char *text) {
    str_addc(s, '"');
    str_addz(s, text);
    str_addc(s, '"');
}

int osr_cc_from_source(const OsrCcSource *cc) {
    Str prefix, exe, script, desc;
    int ok = 1;

    str_init(&prefix); str_init(&exe); str_init(&script); str_init(&desc);

    str_addz(&prefix, osr_mod_home());
    str_addz(&prefix, "/.local/share/");
    str_addz(&prefix, cc->name);
    str_addz(&exe, str_text(&prefix));
    str_addz(&exe, "/bin/");
    str_addz(&exe, cc->name);

    /* Idempotency probe (SS2). */
    if (is_exec(str_text(&exe))) {
        osr_infof("%s already installed at %s - skipping", cc->name, str_text(&prefix));
        goto out;
    }

    say(&desc, "Installing build dependencies for", cc->name);
    if (!osr_pkg_install_step(str_text(&desc), cc->pkgs)) { ok = 0; goto out; }

    /* Clean slate: a half-built tree from a failed run must not be reused. */
    str_reset(&script);
    str_addz(&script, "set -e; rm -rf ");
    quoted(&script, str_text(&prefix));
    str_addz(&script, "; mkdir -p ");
    quoted(&script, str_text(&prefix));
    str_addz(&script, "/bin; git clone --depth 1 ");
    str_addz(&script, cc->repo);
    str_addc(&script, ' ');
    quoted(&script, str_text(&prefix));
    str_addz(&script, "/src");
    say(&desc, "Fetching the source of", cc->name);
    if (!run_sh_user(str_text(&desc), str_text(&script))) { ok = 0; goto out; }

    /* The recipe, in the checkout, with the three paths it may need.
     * MODROOT goes through `cd && pwd` because osr_mod_root is relative
     * when osr was started from its own directory, and the recipe runs
     * somewhere else. */
    str_reset(&script);
    str_addz(&script, "set -e; PREFIX=");
    quoted(&script, str_text(&prefix));
    str_addz(&script, "; SRC=\"$PREFIX/src\"; MODROOT=$(cd ");
    quoted(&script, osr_mod_root());
    str_addz(&script, " && pwd); export PREFIX SRC MODROOT; cd \"$SRC\"; ");
    str_addz(&script, cc->script);
    say(&desc, "Building", cc->name);
    if (!run_sh_user(str_text(&desc), str_text(&script))) { ok = 0; goto out; }

    if (!is_exec(str_text(&exe))) {
        osr_warnf("%s built but left no driver at %s", cc->name, str_text(&exe));
        ok = 0; goto out;
    }

    /* A compiler that targets this box must compile AND run a hello world;
     * with its own bin/ on PATH, because a driver that spawns its own
     * preprocessor by bare name (SmallerC's smlrcc spawns smlrpp) finds it
     * no other way;
     * one that cross-compiles is taken at its word, since its output cannot
     * be executed here. */
    if (cc->hosted) {
        str_reset(&script);
        str_addz(&script, str_text(&prefix));
        str_addz(&script, "/src/hello.c");
        osr_write_user(str_text(&script),
            "#include <stdio.h>\nint main(void){printf(\"ok\\n\");return 0;}\n");
        str_reset(&script);
        str_addz(&script, "set -e; PATH=");
        quoted(&script, str_text(&prefix));
        str_addz(&script, "/bin:$PATH; export PATH; cd ");
        quoted(&script, str_text(&prefix));
        str_addz(&script, "/src; ");
        quoted(&script, str_text(&exe));
        str_addz(&script, " -o hello.out hello.c; ./hello.out");
        say(&desc, "Verifying a hello world with", cc->name);
        if (!run_sh_user(str_text(&desc), str_text(&script))) {
            osr_warnf("%s built but its hello world did not run", cc->name);
            ok = 0; goto out;
        }
    }

    /* ~/.local/bin is on the profile's PATH (the 00-env.zsh layer). Every
     * program the build installed goes there, not only the driver: cproc
     * ships cproc-qbe next to cproc, and SmallerC's driver looks its own
     * smlrpp/smlrc/smlrl up on PATH. */
    str_reset(&script);
    str_addz(&script, "set -e; P=");
    quoted(&script, str_text(&prefix));
    str_addz(&script, "; B=");
    quoted(&script, osr_mod_home());
    str_addz(&script, "/.local/bin; mkdir -p \"$B\"; ln -sf \"$P\"/bin/* \"$B\"/");
    say(&desc, "Linking onto PATH:", cc->name);
    if (!run_sh_user(str_text(&desc), str_text(&script))) { ok = 0; goto out; }

    osr_successf("%s installed at %s", cc->name, str_text(&prefix));

out:
    str_free(&prefix); str_free(&exe); str_free(&script); str_free(&desc);
    return ok;
}
