/* lib/update.c -- `osr update`: replace this binary with a newer one.
 *
 * Two routes, because there are two ways an `osr` comes to exist and updating
 * one is not updating the other:
 *
 *   osr update          the release route. Resolve the latest tag, download
 *                       the artifact built for this OS/arch/libc, and put it
 *                       where the running binary is. This is the route for a
 *                       box that installed os-rice by downloading one file.
 *   osr update --src     the source route. Fetch the sources and rebuild this
 *                       binary out of them with nob, which is the build system
 *                       (nob.c). For a checkout -- the case where a downloaded
 *                       binary would be wrong, see below -- and for anyone who
 *                       would rather compile than trust an artifact.
 *
 * A CHECKOUT CANNOT TAKE THE RELEASE ROUTE, and the refusal is the feature.
 * The `osr` launcher runs nob on every single invocation, on purpose: a `git
 * pull` that never reached the binary would leave the checkout saying one
 * thing and the box doing another. So a release binary dropped into a
 * checkout's build/ survives exactly until the next command, and "osr update"
 * would look like it worked and change nothing. It errors instead, naming
 * --src.
 *
 * THE ARTIFACT IS PICKED, NOT PROBED FOR. .github/workflows/release.yml names
 * every asset osr-<tag>-<os>-<arch>[-<libc>], so the name is derivable from
 * facts this binary already has -- no release JSON to parse, no asset list to
 * walk, and a 404 is a clear "there is no build for this box" rather than a
 * silently wrong download.
 *
 * C89 + POSIX.
 */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "cmds.h"
#include "fetch.h"
#include "git.h"
#include "module.h"

#include <unistd.h>

/* self_path -- the file this process was loaded from.
 *
 * /proc/self/exe first because it is the only answer that survives a PATH
 * lookup, a relative argv[0] and a shim exec'ing through: the launcher does
 * exactly that, and $OSR_BIN is the value it hands down, which is why that is
 * the fallback rather than argv[0]. A binary run some third way has neither
 * and gets told so, rather than overwriting a guess.
 */
static int self_path(Str *out) {
    char buf[OSR_PATH_MAX];
    long n = (long)readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; str_addz(out, buf); return 1; }

    if (env_is_set("OSR_BIN")) { str_addz(out, env_str("OSR_BIN", "")); return 1; }
    return 0;
}

/* tree_root -- the checkout this binary was built from, or "" when it stands
 * alone. nob.c rather than lib/: lib/ is present in the tree a released binary
 * CLONES for its data files, and that tree is not a thing to rebuild from
 * unless it also carries the build system. */
static void tree_root(Str *out) {
    Str probe;
    str_init(&probe);
    str_addzz(&probe, env_str("OSR_ROOT", ""), "/nob.c", (const char *)NULL);
    if (env_is_set("OSR_ROOT") && file_exists(str_text(&probe)))
        str_addz(out, env_str("OSR_ROOT", ""));
    str_free(&probe);
}

/* asset_arch -- uname's spelling of the machine, in the release's spelling.
 * "" for an architecture nothing is built for, which is a clearer error than a
 * 404 on a plausible-looking name. */
static const char *asset_arch(void) {
    const char *a = env_str("OSR_ARCH", "");
    if (strcmp(a, "x86_64") == 0 || strcmp(a, "amd64") == 0) return "x86_64";
    if (strcmp(a, "aarch64") == 0 || strcmp(a, "arm64") == 0) return "aarch64";
    if (a[0] == 'i' && a[1] != '\0' && strcmp(a + 2, "86") == 0) return "x86";
    if (strcmp(a, "x86") == 0) return "x86";
    return "";
}

/* asset_libc -- glibc or musl, by looking for musl's loader.
 *
 * The loader and not `ldd --version`: a musl box's ldd is the loader itself
 * and its version goes to stderr with a nonzero exit, which is exactly the
 * shape a probe misreads. The file either exists or it does not.
 *
 * Getting this wrong is not cosmetic. The glibc artifact is dynamically linked
 * and will not start on musl at all; the musl one is static, so it would start
 * on glibc and then resolve users out of /etc/passwd only -- an LDAP or SSSD
 * account, which §8 resolves OSR_USER through, would stop existing.
 */
static const char *asset_libc(void) {
    static const struct { const char *arch; const char *loader; } musl[] = {
        { "x86_64",  "/lib/ld-musl-x86_64.so.1"  },
        { "aarch64", "/lib/ld-musl-aarch64.so.1" },
        { "x86",     "/lib/ld-musl-i386.so.1"    }
    };
    const char *a = asset_arch();
    size_t i;
    for (i = 0; i < sizeof musl / sizeof musl[0]; i++) {
        if (strcmp(a, musl[i].arch) == 0 && file_exists(musl[i].loader))
            return "-musl";
    }
    return "-glibc";
}

/* asset_name -- osr-<tag>-<slug>, the filename release.yml uploads. */
static int asset_name(Str *out, const char *tag) {
    const char *a = asset_arch();
    if (*a == '\0') {
        osr_warnf("update: no release is built for %s", env_str("OSR_ARCH", "?"));
        return 0;
    }
    str_addzz(out, "osr-", tag, "-linux-", a, asset_libc(), (const char *)NULL);
    return 1;
}

/* put_binary -- move `from` onto `to`, escalating only when the destination
 * needs it. Both halves matter: /usr/local/bin/osr is root's and must stay
 * root's, and a binary under $HOME must NOT come out owned by root because an
 * update happened to be run under sudo. `install` rather than cp + chmod
 * because it sets the mode in the same call and replaces a RUNNING executable
 * by unlinking it first -- a cp into a live binary is ETXTBSY. */
static int put_binary(const char *from, const char *to) {
    char dir[OSR_PATH_MAX];
    char *argv[6];
    int root_needed;

    osr_dirname(to, dir, sizeof(dir));
    root_needed = access(dir, W_OK) != 0;

    argv[0] = (char *)"install";
    argv[1] = (char *)"-m";
    argv[2] = (char *)"0755";
    argv[3] = (char *)from;
    argv[4] = (char *)to;
    argv[5] = NULL;
    return root_needed ? osr_run_step_root("Installing the new osr", argv)
                       : osr_run_step_user("Installing the new osr", argv);
}

static int update_from_release(const char *self) {
    Str tree, tag, asset, url, tmp;
    int ok = 0;

    str_initv(&tree, &tag, &asset, &url, &tmp, (Str *)NULL);
    tree_root(&tree);
    if (tree.len > 0) {
        osr_warnf("update: %s is built from the checkout at %s, and the "
                  "launcher rebuilds it on every run - a downloaded binary "
                  "would be replaced by the next command. Use `osr update "
                  "--src`.", self, str_text(&tree));
        goto out;
    }

    if (!osr_github_latest(&tag, "Kseen715/dotfiles")) goto out;
    if (!asset_name(&asset, str_text(&tag))) goto out;
    str_addzz(&url, "https://github.com/Kseen715/dotfiles/releases/download/",
              str_text(&tag), "/", str_text(&asset), (const char *)NULL);

    /* Downloaded beside nothing and installed in one move, rather than written
     * over the live binary in place: a half-fetched osr that is still
     * executable is the one failure mode worth spending a temp file on. */
    str_addzz(&tmp, osr_tmpdir(), "/", str_text(&asset), (const char *)NULL);
    osr_infof("update: %s", str_text(&url));
    if (osr_fetch_download(str_text(&url), str_text(&tmp), -1) != OSR_NET_OK) {
        osr_warnf("update: could not download %s", str_text(&url));
        goto out;
    }
    ok = put_binary(str_text(&tmp), self);
    (void)unlink(str_text(&tmp));
    if (ok) osr_successf("osr updated to %s", str_text(&tag));

out:
    str_freev(&tree, &tag, &asset, &url, &tmp, (Str *)NULL);
    return ok;
}

/* fetch_sources -- the tree to build from, ready to build.
 *
 * A CHECKOUT IS PULLED, NEVER RESET. osr_git_repo() throws a dirty tree away,
 * which is right for the clone os-rice made and owns and catastrophic for the
 * one somebody is working in -- so a checkout gets `git pull --ff-only`, which
 * refuses rather than destroys when there is local work, and the managed clone
 * gets the idempotent clone-or-update.
 */
static int fetch_sources(Str *root) {
    char dest[OSR_PATH_MAX];
    char *argv[6];
    int ok = 1;

    tree_root(root);
    if (root->len > 0) {
        char repo[OSR_PATH_MAX];
        osr_dirname(str_text(root), repo, sizeof(repo));
        argv[0] = (char *)"git";  argv[1] = (char *)"-C"; argv[2] = repo;
        argv[3] = (char *)"pull"; argv[4] = (char *)"--ff-only"; argv[5] = NULL;
        ok = osr_run_step_user("Updating the checkout", argv);
        if (!ok)
            osr_warnf("update: `git pull --ff-only` in %s did not run clean - "
                      "commit or stash what is there, this will not reset it",
                      repo);
        return ok;
    }

    if (!osr_dotfiles_dest(dest, sizeof(dest))) return 0;
    argv[0] = (char *)"--depth"; argv[1] = (char *)"1"; argv[2] = NULL;
    if (!osr_have_cmd("git")) {
        static const char *const git_pkg[] = { "git", NULL };
        (void)osr_pkg_install_step("Installing git", git_pkg);
    }
    osr_resolve_user(NULL);
    if (!osr_git_repo("os-rice dotfiles", env_str("OSR_REPO_URL", OSR_DOTFILES_REPO_URL),
                      dest, argv))
        return 0;
    str_addzz(root, dest, "/os-rice", (const char *)NULL);
    return 1;
}

/* build_tree -- nob.c is the build system; this is its one-time bootstrap line
 * followed by the build, which is exactly what the launcher does on a fresh
 * checkout. chdir rather than `sh -c 'cd ... && ...'`: nob resolves its inputs
 * from the working directory, and a path pushed through a shell is a quoting
 * bug waiting for a checkout with a space in its name. */
static int build_tree(const char *root, Str *out_bin) {
    char cwd[OSR_PATH_MAX];
    char *argv[5];
    Str nob, src;
    int ok = 0;

    str_initv(&nob, &src, (Str *)NULL);
    str_addzz(&src, root, "/nob.c", (const char *)NULL);
    str_addzz(&nob, root, "/build/nob", (const char *)NULL);
    str_addzz(out_bin, root, "/build/osr", (const char *)NULL);

    if (getcwd(cwd, sizeof(cwd)) == NULL) goto out;
    if (chdir(root) != 0) { osr_warnf("update: cannot enter %s", root); goto out; }

    if (!file_exists(str_text(&nob))) {
        Str build;
        str_init(&build);
        str_addzz(&build, root, "/build", (const char *)NULL);
        (void)osr_mkdir_p(str_text(&build));
        str_free(&build);
        argv[0] = (char *)env_str("CC", "cc");
        argv[1] = (char *)"-o"; argv[2] = nob.p; argv[3] = src.p; argv[4] = NULL;
        if (!osr_run_step_user("Bootstrapping nob", argv)) goto back;
    }

    argv[0] = nob.p; argv[1] = (char *)"static"; argv[2] = NULL;
    ok = osr_run_step_user("Building osr", argv);

back:
    (void)chdir(cwd);
out:
    str_freev(&nob, &src, (Str *)NULL);
    return ok;
}

static int update_from_source(const char *self) {
    Str root, built;
    int ok = 0;

    str_initv(&root, &built, (Str *)NULL);
    if (!fetch_sources(&root)) goto out;
    if (!build_tree(str_text(&root), &built)) goto out;

    /* Inside a checkout the build already wrote the file this process runs
     * from; copying it onto itself would be a no-op at best and an ETXTBSY at
     * worst. Standing alone, the freshly built binary has to be moved to where
     * the old one is, which is the whole point of the route. */
    ok = 1;
    if (strcmp(str_text(&built), self) != 0) ok = put_binary(str_text(&built), self);
    if (ok) osr_successf("osr rebuilt from %s", str_text(&root));

out:
    str_freev(&root, &built, (Str *)NULL);
    return ok;
}

int osr_update_main(int argc, char **argv) {
    Str self;
    int src = 0, i, ok;

    /* argv[0] is the verb itself (osr.c dispatches argc-1/argv+1). */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--src") == 0) src = 1;
        else {
            fprintf(stderr, "usage: osr update [--src]\n");
            return 2;
        }
    }

    str_init(&self);
    if (!self_path(&self)) {
        str_free(&self);
        osr_warn("update: cannot tell which file this process runs from");
        return 1;
    }

    /* OSR_ARCH, which the asset name is built out of. */
    osr_detect_export("all");
    ok = src ? update_from_source(str_text(&self))
             : update_from_release(str_text(&self));
    str_free(&self);
    return ok ? 0 : 1;
}
