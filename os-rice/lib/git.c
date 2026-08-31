/* lib/git.c -- C port of lib/git.sh. See lib/git.h for the contract.
 *
 * Every git invocation here goes through osr_run_user*, which is as_user: the
 * repos live in the riced account's home, and a root-owned object inside one
 * is the failure this file spends its first four lines repairing.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "git.h"
#include "cmds.h"
#include "fetch.h"
#include "module.h"

/* argv_git -- `git -C <dir> <rest...>`, the shape every probe below wants.
 * rest is NULL-terminated and short; the vector is a caller-owned array. */
static void argv_git(char *v[], size_t cap, const char *dir, const char *const rest[]) {
    size_t n = 0;
    size_t i;
    v[n++] = (char *)"git";
    if (dir != NULL) {
        v[n++] = (char *)"-C";
        v[n++] = (char *)dir;
    }
    for (i = 0; rest[i] != NULL && n + 1 < cap; i++) v[n++] = (char *)rest[i];
    v[n] = NULL;
}

/* remote_matches -- does the repo's origin name the same place as url? sh
 * accepted three spellings, because a clone URL and what git reports back
 * differ by a .git the caller may or may not have written. */
static int remote_matches(const char *remote, const char *url) {
    size_t url_len = strlen(url);
    size_t rem_len = strlen(remote);

    if (strcmp(remote, url) == 0) return 1;
    /* "$_gr_url.git" */
    if (rem_len == url_len + 4 && strncmp(remote, url, url_len) == 0 &&
        strcmp(remote + url_len, ".git") == 0) return 1;
    /* "${_gr_url%.git}" -- the suffix removed only when it is there. */
    if (url_len > 4 && strcmp(url + url_len - 4, ".git") == 0 &&
        rem_len == url_len - 4 && strncmp(remote, url, rem_len) == 0) return 1;
    return 0;
}

/* repo_dirty -- worktree or index has changes. Both probes are `--quiet`, so
 * they say it with an exit status and print nothing either way. */
static int repo_dirty(const char *dir) {
    char *v[8];
    const char *worktree[3];
    const char *cached[4];

    worktree[0] = "diff"; worktree[1] = "--quiet"; worktree[2] = NULL;
    cached[0] = "diff"; cached[1] = "--cached"; cached[2] = "--quiet"; cached[3] = NULL;

    /* sh: `! as_user git ... diff --quiet` -- any non-zero, including the 127
     * of a missing git, counted as dirty. */
    argv_git(v, sizeof v / sizeof v[0], dir, worktree);
    if (osr_run_user_quiet(v) != 0) return 1;

    argv_git(v, sizeof v / sizeof v[0], dir, cached);
    return osr_run_user_quiet(v) != 0;
}

/* clone_into -- `as_user git clone [args...] <url> <dir>`, fatal on failure,
 * which is what check_error did to every caller. */
static void clone_into(const char *name, const char *url, const char *dir,
                       char *const clone_args[]) {
    char *v[32];
    size_t n = 0;
    size_t i;
    int rc;

    v[n++] = (char *)"git";
    v[n++] = (char *)"clone";
    if (clone_args != NULL)
        for (i = 0; clone_args[i] != NULL && n + 3 < sizeof v / sizeof v[0]; i++)
            v[n++] = clone_args[i];
    v[n++] = (char *)url;
    v[n++] = (char *)dir;
    v[n] = NULL;

    rc = osr_run_user(v);
    if (rc != 0) osr_die("failed to clone %s (exit %d)", name, rc);
}

int osr_git_repo(const char *name, const char *url, const char *dir,
                 char *const clone_args[]) {
    Str git_dir;
    Str remote;
    int have_git_dir;

    if (osr_theme_only()) return osr_theme_only_skip("git_repo");

    /* Fix root-owned files from a previous sudo run (§8): the git work below
     * runs as OSR_USER, so every byte under the repo must be that user's. */
    if (dir_exists(dir)) {
        char *chown_v[5];
        Str owner;
        str_init(&owner);
        str_addz(&owner, osr_mod_user());
        str_addc(&owner, ':');
        str_addz(&owner, osr_mod_user());
        chown_v[0] = (char *)"chown";
        chown_v[1] = (char *)"-R";
        chown_v[2] = (char *)str_text(&owner);
        chown_v[3] = (char *)dir;
        chown_v[4] = NULL;
        (void)osr_run_root_quiet(chown_v);
        str_free(&owner);
    }

    str_init(&git_dir);
    str_addz(&git_dir, dir);
    str_addz(&git_dir, "/.git");
    have_git_dir = dir_exists(str_text(&git_dir));
    str_free(&git_dir);

    if (!have_git_dir) {
        clone_into(name, url, dir, clone_args);
        return 1;
    }

    str_init(&remote);
    {
        char *v[8];
        const char *rest[4];
        rest[0] = "remote"; rest[1] = "get-url"; rest[2] = "origin"; rest[3] = NULL;
        argv_git(v, sizeof v / sizeof v[0], dir, rest);
        /* sh took `|| echo ""`, so a failure is simply an empty remote. */
        if (!osr_run_user_capture(v, &remote)) str_reset(&remote);
        str_trim_trailing(&remote, '\n');
    }

    if (remote_matches(str_text(&remote), url)) {
        char *v[8];
        int rc;
        if (repo_dirty(dir)) {
            osr_infof("%s has local changes - resetting to clean state", name);
            {
                const char *reset[4];
                reset[0] = "reset"; reset[1] = "--hard"; reset[2] = "HEAD"; reset[3] = NULL;
                argv_git(v, sizeof v / sizeof v[0], dir, reset);
                (void)osr_run_user_quiet(v);
            }
            {
                const char *clean[3];
                clean[0] = "clean"; clean[1] = "-fd"; clean[2] = NULL;
                argv_git(v, sizeof v / sizeof v[0], dir, clean);
                (void)osr_run_user_quiet(v);
            }
        }
        {
            const char *pull[3];
            pull[0] = "pull"; pull[1] = "--ff-only"; pull[2] = NULL;
            argv_git(v, sizeof v / sizeof v[0], dir, pull);
            rc = osr_run_user(v);
        }
        if (rc != 0) osr_die("failed to update %s (exit %d)", name, rc);
    } else {
        char *rm_v[4];
        osr_infof("%s points at a different remote - recloning", name);
        rm_v[0] = (char *)"rm";
        rm_v[1] = (char *)"-rf";
        rm_v[2] = (char *)dir;
        rm_v[3] = NULL;
        (void)osr_run(rm_v);
        clone_into(name, url, dir, clone_args);
    }
    str_free(&remote);
    return 1;
}

int osr_zsh_plugin(const char *name, const char *url) {
    Str dir;
    char *args[3];
    int ok;

    if (osr_theme_only()) return osr_theme_only_skip("zsh_plugin");

    str_init(&dir);
    str_addz(&dir, osr_mod_home());
    str_addz(&dir, "/.oh-my-zsh/custom/plugins/");
    str_addz(&dir, name);

    args[0] = (char *)"--depth";
    args[1] = (char *)"1";
    args[2] = NULL;

    ok = osr_git_repo(name, url, str_text(&dir), args);
    str_free(&dir);
    return ok;
}

#define OMZ_URL     "https://github.com/ohmyzsh/ohmyzsh.git"
#define OMZ_INSTALL "https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh"

/* omz_path -- $OSR_HOME/.oh-my-zsh<suffix>. */
static void omz_path(Str *out, const char *suffix) {
    str_reset(out);
    str_addz(out, osr_mod_home());
    str_addz(out, "/.oh-my-zsh");
    str_addz(out, suffix);
}

/* user_rm_rf / user_mv -- the two file moves install_omz makes, both as_user
 * because the result must belong to the riced account. */
static int user_rm_rf(const char *path) {
    char *v[4];
    v[0] = (char *)"rm"; v[1] = (char *)"-rf"; v[2] = (char *)path; v[3] = NULL;
    return osr_run_user(v) == 0;
}

static int user_mv(const char *src, const char *dst) {
    char *v[4];
    v[0] = (char *)"mv"; v[1] = (char *)src; v[2] = (char *)dst; v[3] = NULL;
    return osr_run_user(v) == 0;
}

/* patch_installer -- upstream's install.sh with its interactive bits removed,
 * the C form of `sed 's:env zsh -l::g; s:chsh -s .*$:true:g'`. Both edits are
 * per line and in that order: every "env zsh -l" disappears, and the first
 * "chsh -s " on a line takes the rest of the line with it.
 */
static void patch_installer(Str *out, const char *script, size_t len) {
    size_t pos = 0;
    Line line;

    while (next_line(script, len, &pos, &line)) {
        Str work;
        const char *p;
        const char *chsh;

        str_init(&work);
        p = line.start;
        {
            size_t left = line.len;
            const char *hit;
            while (left > 0 && (hit = memchr(p, 'e', left)) != NULL) {
                size_t before = (size_t)(hit - p);
                if (left - before >= 10 && memcmp(hit, "env zsh -l", 10) == 0) {
                    str_add(&work, p, before);
                    p = hit + 10;
                    left -= before + 10;
                } else {
                    str_add(&work, p, before + 1);
                    p = hit + 1;
                    left -= before + 1;
                }
            }
            str_add(&work, p, left);
        }

        chsh = strstr(str_text(&work), "chsh -s ");
        if (chsh != NULL) {
            str_add(out, str_text(&work), (size_t)(chsh - str_text(&work)));
            str_addz(out, "true");
        } else {
            str_addz(out, str_text(&work));
        }
        str_addc(out, '\n');
        str_free(&work);
    }
}

int osr_install_omz(void) {
    Str core;
    Str script;
    Str patched;
    int rc;

    if (osr_theme_only()) return osr_theme_only_skip("install_omz");

    str_init(&core);
    omz_path(&core, "/oh-my-zsh.sh");
    if (file_exists(str_text(&core))) {
        osr_infof("oh-my-zsh already installed - skipping");
        str_free(&core);
        return 1;
    }
    omz_path(&core, "");

    /* The stub case. Upstream's installer refuses to write into an existing
     * $ZSH directory, so seed the core with a plain clone and carry the
     * existing custom/ across - that is where osr_zsh_plugin put the plugins,
     * and re-cloning them would be the slow way to the same place. */
    if (dir_exists(str_text(&core))) {
        Str fresh;
        Str custom;
        char *clone_v[7];
        int clone_rc;

        str_init(&fresh);
        omz_path(&fresh, ".osr-new");
        (void)user_rm_rf(str_text(&fresh));

        clone_v[0] = (char *)"git";
        clone_v[1] = (char *)"clone";
        clone_v[2] = (char *)"--depth";
        clone_v[3] = (char *)"1";
        clone_v[4] = (char *)OMZ_URL;
        clone_v[5] = (char *)str_text(&fresh);
        clone_v[6] = NULL;
        clone_rc = osr_run_user(clone_v);
        if (clone_rc != 0) osr_die("failed to clone oh-my-zsh (exit %d)", clone_rc);

        str_init(&custom);
        omz_path(&custom, "/custom");
        if (dir_exists(str_text(&custom))) {
            Str fresh_custom;
            str_init(&fresh_custom);
            str_addz(&fresh_custom, str_text(&fresh));
            str_addz(&fresh_custom, "/custom");
            (void)user_rm_rf(str_text(&fresh_custom));
            (void)user_mv(str_text(&custom), str_text(&fresh_custom));
            str_free(&fresh_custom);
        }
        (void)user_rm_rf(str_text(&core));
        (void)user_mv(str_text(&fresh), str_text(&core));

        str_free(&custom);
        str_free(&fresh);
        str_free(&core);
        return 1;
    }
    str_free(&core);

    str_init(&script);
    (void)osr_fetch_buffer(&script, OMZ_INSTALL);

    str_init(&patched);
    patch_installer(&patched, str_text(&script), script.len);
    /* `$( )` dropped the trailing newlines and `printf '%s'` never added one
     * back, so the installer is fed exactly this much. */
    str_trim_trailing(&patched, '\n');

    {
        /* The payload goes through a temporary file rather than a pipe this
         * process would have to keep writing to: the reader is a child we
         * wait for, so a script larger than one pipe buffer would deadlock
         * against itself. */
        Str tmp;
        int fd;
        char *v[7];

        str_init(&tmp);
        str_addz(&tmp, env_str("TMPDIR", "/tmp"));
        str_addz(&tmp, "/osr-omz-");
        str_addl(&tmp, (long)getpid());
        fd = open(str_text(&tmp), O_RDWR | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) osr_die("failed to install oh-my-zsh (exit 1)");
        remove(str_text(&tmp));
        {
            const char *text = str_text(&patched);
            size_t left = patched.len;
            while (left > 0) {
                long n = (long)write(fd, text, left);
                if (n <= 0) osr_die("failed to install oh-my-zsh (exit 1)");
                text += n;
                left -= (size_t)n;
            }
        }
        lseek(fd, 0, SEEK_SET);

        /* sh passed `"" --unattended --skip-chsh`; the empty first argument is
         * the installer's own convention for "no custom remote". */
        v[0] = (char *)"sh";
        v[1] = (char *)"-s";
        v[2] = (char *)"--";
        v[3] = (char *)"";
        v[4] = (char *)"--unattended";
        v[5] = (char *)"--skip-chsh";
        v[6] = NULL;
        rc = osr_run_user_in(v, fd);
        close(fd);
        str_free(&tmp);
    }

    str_free(&patched);
    str_free(&script);
    if (rc != 0) osr_die("failed to install oh-my-zsh (exit %d)", rc);
    return 1;
}

static int git_usage(void) {
    fputs("usage: osr git <subcommand> [args]\n\n", stderr);
    fputs("  repo <name> <url> <dir> [clone-args...]   clone or update\n", stderr);
    fputs("  plugin <name> <url>                       an oh-my-zsh custom plugin\n", stderr);
    fputs("  omz                                       install oh-my-zsh\n", stderr);
    return 2;
}

int osr_git_main(int argc, char **argv) {
    if (argc < 2) return git_usage();

    if (strcmp(argv[1], "repo") == 0 && argc >= 5) {
        char **extra = (argc > 5) ? &argv[5] : NULL;
        return osr_git_repo(argv[2], argv[3], argv[4], extra) ? 0 : 1;
    }
    if (strcmp(argv[1], "plugin") == 0 && argc == 4)
        return osr_zsh_plugin(argv[2], argv[3]) ? 0 : 1;
    if (strcmp(argv[1], "omz") == 0 && argc == 2)
        return osr_install_omz() ? 0 : 1;

    return git_usage();
}
