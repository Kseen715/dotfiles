/* lib/build.c -- lib/build.sh's source: providers.
 *
 * Each builder resolves its own version (osr_github_latest, G4) and arch
 * (OSR_ARCH / OSR_ARCH_DEB, G8), so the pkgmap row and the rice list stay
 * logic-free. Idempotency is owned by the caller's `command -v <name>` probe
 * (§2, §4) -- except where a builder says otherwise, which is fzf: an old
 * distro fzf satisfies that probe and would never be replaced, so the version
 * re-check lives in the builder.
 *
 * The plumbing a shell got for free -- mktemp, find, rm -rf -- is done in
 * process here rather than forked, since that is the point of the port. The
 * commands that MATTER (the download, tar, install, apt-get) are the same
 * argv the sh original ran, in the same order, which is what the parity test
 * compares.
 *
 * C89 + POSIX.
 */
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "build.h"
#include "cmds.h"
#include "fetch.h"
#include "module.h"

/* --- small local plumbing -------------------------------------------------- */

/* tmp_root -- ${TMPDIR:-/tmp}, without the trailing slash sh never adds. */
static const char *tmp_root(void) {
    const char *t = env_str("TMPDIR", "/tmp");
    return *t != '\0' ? t : "/tmp";
}

/* make_tmp_dir -- `mktemp -d`. */
static int make_tmp_dir(Str *out) {
    Str tpl;
    str_init(&tpl);
    str_addz(&tpl, tmp_root());
    str_addz(&tpl, "/tmp.XXXXXX");
    if (mkdtemp(tpl.p) == NULL) {
        str_free(&tpl);
        return 0;
    }
    str_reset(out);
    str_addz(out, tpl.p);
    str_free(&tpl);
    return 1;
}

/* rm_rf -- `rm -rf <path>`, run rather than walked: cleanup must not be the
 * thing that fails, and rm already knows every corner case. */
static void rm_rf(const char *path) {
    char *argv[4];
    argv[0] = (char *)"rm"; argv[1] = (char *)"-rf";
    argv[2] = (char *)path; argv[3] = NULL;
    (void)osr_run_quiet(argv);
}

/* find_file -- `find <dir> -type f -name <name> | head -n 1`: the first regular
 * file of that name anywhere below dir. Returns 0 when there is none. */
static int find_file(Str *out, const char *dir, const char *name) {
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = 0;

    if (d == NULL) return 0;
    while (!found && (e = readdir(d)) != NULL) {
        Str path;
        struct stat st;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        str_init(&path);
        str_addz(&path, dir);
        str_addc(&path, '/');
        str_addz(&path, e->d_name);
        if (lstat(path.p, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                found = find_file(out, path.p, name);
            } else if (S_ISREG(st.st_mode) && strcmp(e->d_name, name) == 0) {
                str_reset(out);
                str_addz(out, path.p);
                found = 1;
            }
        }
        str_free(&path);
    }
    closedir(d);
    return found;
}

/* has_text -- `grep <needle>` over len bytes that are not NUL-terminated. */
static int has_text(const char *hay, size_t len, const char *needle) {
    size_t n = strlen(needle);
    size_t i;

    if (n > len) return 0;
    for (i = 0; i + n <= len; i++) {
        if (memcmp(hay + i, needle, n) == 0) return 1;
    }
    return 0;
}

/* run_ok -- run argv with its output straight through, as the sh builders did. */
static int run_ok(char *const argv[]) { return osr_run(argv) == 0; }

/* --- the shared primitives ------------------------------------------------- */

int osr_install_tarball_bin(const char *url, const char *bin) {
    Str tmp, tar_path, found, dest;
    char *argv[7];
    int ok;

    str_init(&tmp); str_init(&tar_path); str_init(&found); str_init(&dest);
    if (!make_tmp_dir(&tmp)) osr_die("failed to create a temporary directory");
    str_addz(&tar_path, str_text(&tmp));
    str_addz(&tar_path, "/pkg.tar");

    if (!osr_fetch_download(url, tar_path.p, 0)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to download %s", url);
    }
    argv[0] = (char *)"tar"; argv[1] = (char *)"-xf"; argv[2] = tar_path.p;
    argv[3] = (char *)"-C"; argv[4] = tmp.p; argv[5] = NULL;
    if (!run_ok(argv)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to extract %s", url);
    }
    if (!find_file(&found, str_text(&tmp), bin)) {
        rm_rf(str_text(&tmp));
        osr_die("%s not found in %s", bin, url);
    }
    str_addz(&dest, "/usr/local/bin/");
    str_addz(&dest, bin);
    argv[0] = (char *)"install"; argv[1] = (char *)"-m"; argv[2] = (char *)"0755";
    argv[3] = found.p; argv[4] = dest.p; argv[5] = NULL;
    ok = osr_run_root(argv) == 0;
    rm_rf(str_text(&tmp));

    str_free(&tmp); str_free(&tar_path); str_free(&found); str_free(&dest);
    return ok;
}

/* install_zip_bins -- _osr_install_zip_bins: the .zip counterpart of
 * osr_install_tarball_bin, for an upstream that ships no tarball (yazi), and
 * for several binaries out of the one archive. Unlike the tarball primitive it
 * WARNS AND RETURNS 0 rather than stopping the run, because its only caller has
 * a fallback route and a hard exit here would take that away. */
static int install_zip_bins(const char *url, const char *const bins[]) {
    Str tmp, zip_path, found, dest;
    char *argv[7];
    size_t i;
    int ok = 1;

    if (!osr_have_cmd("unzip")) {
        const char *want[2];
        want[0] = "unzip"; want[1] = NULL;
        (void)osr_pkg_install(want);
    }
    if (!osr_have_cmd("unzip")) {
        osr_warnf("unzip not available for %s", url);
        return 0;
    }

    str_init(&tmp); str_init(&zip_path); str_init(&found); str_init(&dest);
    if (!make_tmp_dir(&tmp)) osr_die("failed to create a temporary directory");
    str_addz(&zip_path, str_text(&tmp));
    str_addz(&zip_path, "/pkg.zip");

    if (!osr_fetch_download(url, zip_path.p, 0)) {
        osr_warnf("failed to download %s", url);
        ok = 0;
    }
    if (ok) {
        argv[0] = (char *)"unzip"; argv[1] = (char *)"-q"; argv[2] = (char *)"-o";
        argv[3] = zip_path.p; argv[4] = (char *)"-d"; argv[5] = tmp.p; argv[6] = NULL;
        if (!run_ok(argv)) {
            osr_warnf("failed to extract %s", url);
            ok = 0;
        }
    }
    for (i = 0; ok && bins[i] != NULL; i++) {
        if (!find_file(&found, str_text(&tmp), bins[i])) {
            osr_warnf("%s not found in %s", bins[i], url);
            ok = 0;
            break;
        }
        str_reset(&dest);
        str_addz(&dest, "/usr/local/bin/");
        str_addz(&dest, bins[i]);
        argv[0] = (char *)"install"; argv[1] = (char *)"-m"; argv[2] = (char *)"0755";
        argv[3] = found.p; argv[4] = dest.p; argv[5] = NULL;
        if (osr_run_root(argv) != 0) {
            osr_warnf("failed to install %s", bins[i]);
            ok = 0;
            break;
        }
    }
    rm_rf(str_text(&tmp));

    str_free(&tmp); str_free(&zip_path); str_free(&found); str_free(&dest);
    return ok;
}

/* install_local_deb -- the tail both .deb builders share: download it next to
 * the other temporaries, hand the file to apt-get (which pulls its deps), then
 * drop it whether or not the install worked. */
static int install_local_deb(const char *url, const char *deb, const char *what) {
    Str tmp;
    char *argv[8];
    int rc;

    str_init(&tmp);
    str_addz(&tmp, tmp_root());
    str_addc(&tmp, '/');
    str_addz(&tmp, deb);
    if (!osr_fetch_download(url, tmp.p, 0)) osr_die("failed to download %s", url);

    argv[0] = (char *)"env"; argv[1] = (char *)"DEBIAN_FRONTEND=noninteractive";
    argv[2] = (char *)"apt-get"; argv[3] = (char *)"install"; argv[4] = (char *)"-y";
    argv[5] = tmp.p; argv[6] = NULL;
    rc = osr_run_root(argv);
    (void)unlink(tmp.p);
    str_free(&tmp);
    if (rc != 0) osr_die("failed to install %s from %s", what, deb);
    return 1;
}

/* tag_of -- the newest release tag of a repo, or a hard stop: a builder with no
 * version has nothing to download. */
static void tag_of(Str *out, const char *repo) {
    if (!osr_github_latest(out, repo)) osr_die("failed to resolve the latest release of %s", repo);
}

/* arch -- $OSR_ARCH / $OSR_ARCH_DEB, which osr_detect exported. */
static const char *arch(void)     { return env_str("OSR_ARCH", ""); }
static const char *arch_deb(void) { return env_str("OSR_ARCH_DEB", ""); }

/* --- the builders ---------------------------------------------------------- */

/* provide_yazi_bin -- Yazi from its official prebuilt release archive, falling
 * back to `cargo install` (the crates route upstream documents) where there is
 * no asset for this target or the download does not come through. Upstream
 * ships one .zip holding BOTH binaries -- `yazi` (the TUI) and `ya` (the
 * package/plugin CLI modules/yazi.sh's package.toml layer needs) -- so either
 * route installs the pair.
 *
 * The fallback lives inside the one builder rather than spanning two pkgmap
 * rows: the row still resolves to exactly one method, and cargo only runs when
 * the binary route is genuinely unavailable here. */
static int provide_yazi_bin(void) {
    const char *a = arch();
    const char *target = NULL;
    const char *bins[3];
    Str tag, url;
    int ok = 0;

    if (strcmp(a, "x86_64") == 0)       target = "x86_64-unknown-linux-gnu";
    else if (strcmp(a, "aarch64") == 0) target = "aarch64-unknown-linux-gnu";

    bins[0] = "yazi"; bins[1] = "ya"; bins[2] = NULL;
    if (target == NULL) {
        osr_warnf("no yazi release binary for arch %s - falling back to cargo", a);
    } else {
        str_init(&tag); str_init(&url);
        /* Quiet: an unreachable API is a reason to take the cargo route, not to
         * end the run. lib/build.sh spent a subshell on catching error() here
         * for exactly that. */
        if (osr_github_latest_quiet(&tag, "sxyazi/yazi")) {          /* v26.5.6 */
            osr_infof("installing yazi %s from the upstream release binary", str_text(&tag));
            str_addz(&url, "https://github.com/sxyazi/yazi/releases/download/");
            str_addz(&url, str_text(&tag));
            str_addz(&url, "/yazi-");
            str_addz(&url, target);
            str_addz(&url, ".zip");
            ok = install_zip_bins(str_text(&url), bins);
        }
        str_free(&tag); str_free(&url);
        if (ok) return 1;
        osr_warnf("yazi release binary unavailable (%s) - falling back to cargo", target);
    }
    /* Needs the toolchain: list `rust` before `yazi` in the rice when a target
     * can land here (manifest order is the dependency graph, §4). osr_pkg_cargo
     * carries its own §2 probe and the "install 'rust' first" message.
     * The sh function's status was the LAST call's, and so is this one's. */
    (void)osr_pkg_cargo("yazi", "yazi-fm");     /* the TUI */
    return osr_pkg_cargo("ya", "yazi-cli");     /* the `ya` plugin/package CLI */
}

/* --- zig ---------------------------------------------------------------------
 * Zig installs as a whole TREE (it needs its lib/ beside the binary), so this
 * is the one builder that does not end in `install <bin> /usr/local/bin`: the
 * tarball is unpacked into /usr/local/zig-<version> and a symlink points at it.
 * The exact tarball URL is resolved out of index.json rather than composed,
 * because the asset naming changed mid-flight: zig-<arch>-linux on 0.15+,
 * zig-linux-<arch> on <= 0.14. */

/* zig_candidates -- lib/build.sh's
 *   grep -oE 'https://ziglang\.org/download/[0-9][0-9.]+/[^"]+\.tar\.xz'
 *   | grep -E "zig-(<m>-linux|linux-<m>)-"
 * Every match, one per line, in index order -- which is what the `head -n 1`
 * downstream depends on, since the index lists the newest release first.
 *
 * Two details of grep the loop has to keep: `[^"]+` may cross neither a quote
 * nor a LINE BOUNDARY, and the match is leftmost-LONGEST, so within one such
 * run it is the LAST `.tar.xz` that ends the match, not the first. */
static void zig_candidates(Str *out, const char *json, const char *m) {
    static const char base[] = "https://ziglang.org/download/";
    const size_t blen = sizeof(base) - 1;
    const char *p = json;
    Str new_style, old_style;

    str_init(&new_style);
    str_addz(&new_style, "zig-"); str_addz(&new_style, m); str_addz(&new_style, "-linux-");
    str_init(&old_style);
    str_addz(&old_style, "zig-linux-"); str_addz(&old_style, m); str_addc(&old_style, '-');
    str_reset(out);

    while ((p = strstr(p, base)) != NULL) {
        const char *q = p + blen;
        const char *run, *end, *hit, *last = NULL;

        if (*q < '0' || *q > '9') { p = q; continue; }             /* [0-9] */
        while ((*q >= '0' && *q <= '9') || *q == '.') q++;         /* [0-9.]+ */
        if (*q != '/') { p = q; continue; }
        run = q + 1;
        for (end = run; *end != '\0' && *end != '"' && *end != '\n'; end++) ;
        for (hit = run; (hit = strstr(hit, ".tar.xz")) != NULL; hit++) {
            if (hit + 7 > end) break;
            if (hit > run) last = hit;                             /* [^"]+ is one or more */
        }
        if (last == NULL) { p = run; continue; }
        if (has_text(p, (size_t)(last + 7 - p), str_text(&new_style)) ||
            has_text(p, (size_t)(last + 7 - p), str_text(&old_style))) {
            str_add(out, p, (size_t)(last + 7 - p));
            str_addc(out, '\n');
        }
        p = last + 7;
    }
    str_free(&new_style); str_free(&old_style);
}

/* zig_url_version -- the sed script lib/build.sh runs over the resolved URL to
 * pull the version out of its `/download/<version>/` segment. That script's
 * leading `.` `*` is greedy, so it is the LAST such segment that names the
 * version. Returns 0 when nothing matches, which for a URL that came
 * out of zig_candidates cannot happen -- the caller keeps sed's no-match
 * behaviour anyway, which was to pass the line through unchanged. */
static int zig_url_version(Str *out, const char *url) {
    static const char seg[] = "/download/";
    const char *p = url, *hit = NULL, *start, *q;

    while ((p = strstr(p, seg)) != NULL) { hit = p; p += sizeof(seg) - 1; }
    if (hit == NULL) return 0;
    start = hit + sizeof(seg) - 1;
    if (*start < '0' || *start > '9') return 0;
    for (q = start; (*q >= '0' && *q <= '9') || *q == '.'; q++) ;
    if (*q != '/') return 0;
    str_reset(out);
    str_add(out, start, (size_t)(q - start));
    return 1;
}

int osr_build_zig(const char *want) {
    const char *a = arch();
    const char *m;
    Str json, cands, url, ver, dir, exe;
    char *argv[7];
    size_t pos = 0;
    Line line;
    int picked = 0;

    if (strcmp(a, "x86_64") == 0)       m = "x86_64";
    else if (strcmp(a, "aarch64") == 0) m = "aarch64";
    else                                { osr_die("no zig tarball for arch %s", a); return 0; }

    {                                   /* the tarball is .tar.xz, §1a */
        const char *xz[2];
        xz[0] = "xz"; xz[1] = NULL;
        (void)osr_pkg_install(xz);
    }

    str_init(&json); str_init(&cands); str_init(&url);
    (void)osr_fetch_buffer(&json, "https://ziglang.org/download/index.json");
    zig_candidates(&cands, str_text(&json), m);
    str_free(&json);

    /* `grep "/$want/" | head -n 1`, or plain `head -n 1` for the newest. */
    while (!picked && next_line(str_text(&cands), cands.len, &pos, &line)) {
        if (want != NULL && *want != '\0') {
            Str pat;
            int hit;
            str_init(&pat);
            str_addc(&pat, '/'); str_addz(&pat, want); str_addc(&pat, '/');
            hit = has_text(line.start, line.len, str_text(&pat));
            str_free(&pat);
            if (!hit) continue;
        }
        str_add(&url, line.start, line.len);
        picked = 1;
    }
    str_free(&cands);
    if (!picked)
        osr_die("no zig tarball found (version='%s', arch=%s)",
                (want != NULL && *want != '\0') ? want : "latest", m);

    str_init(&ver);
    if (!zig_url_version(&ver, str_text(&url))) str_addz(&ver, str_text(&url));
    str_init(&dir);
    str_addz(&dir, "/usr/local/zig-");
    str_addz(&dir, str_text(&ver));
    str_init(&exe);
    str_addz(&exe, str_text(&dir));
    str_addz(&exe, "/zig");

    if (access(str_text(&exe), X_OK) != 0) {
        Str tmp, tar_path;

        str_init(&tmp); str_init(&tar_path);
        if (!make_tmp_dir(&tmp)) osr_die("failed to create a temporary directory");
        str_addz(&tar_path, str_text(&tmp));
        str_addz(&tar_path, "/zig.tar.xz");
        if (!osr_fetch_download(str_text(&url), tar_path.p, 0)) {
            rm_rf(str_text(&tmp));
            osr_die("failed to download %s", str_text(&url));
        }
        argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p"; argv[2] = dir.p; argv[3] = NULL;
        (void)osr_run_root(argv);
        argv[0] = (char *)"tar"; argv[1] = (char *)"-xf"; argv[2] = tar_path.p;
        argv[3] = (char *)"-C"; argv[4] = dir.p;
        argv[5] = (char *)"--strip-components=1"; argv[6] = NULL;
        if (osr_run_root(argv) != 0) {
            rm_rf(str_text(&tmp));
            osr_die("failed to extract zig %s", str_text(&ver));
        }
        rm_rf(str_text(&tmp));
        str_free(&tmp); str_free(&tar_path);
    }
    argv[0] = (char *)"ln"; argv[1] = (char *)"-sf"; argv[2] = exe.p;
    argv[3] = (char *)"/usr/local/bin/zig"; argv[4] = NULL;
    (void)osr_run_root(argv);

    str_free(&url); str_free(&ver); str_free(&dir); str_free(&exe);
    return 1;
}

/* provide_zig -- the pkgmap row's entry point: no argument, so the version is
 * whatever $ZIG_VERSION pins, or the newest stable. A caller that needs an
 * EXACT one (ghostty reads the version its source tree pins) calls
 * osr_build_zig directly instead. */
static int provide_zig(void) { return osr_build_zig(env_str("ZIG_VERSION", "")); }

/* --- ghostty -----------------------------------------------------------------
 * The install prefers a prebuilt community binary over a source build wherever
 * one exists (https://ghostty.org/docs/install/binary). Native packages pass
 * through pkgmap; these two cover the rest, and the Zig source build (still
 * lib/build.sh's) is the last-resort fallback. */

/* provide_ghostty_copr -- Fedora community binary via COPR (scottames/ghostty). */
static int provide_ghostty_copr(void) {
    char *argv[6];
    int rc;

    {
        const char *deps[2];
        deps[0] = "dnf-plugins-core"; deps[1] = NULL;             /* provides `dnf copr` */
        (void)osr_pkg_install(deps);
    }
    argv[0] = (char *)"dnf"; argv[1] = (char *)"copr"; argv[2] = (char *)"enable";
    argv[3] = (char *)"-y"; argv[4] = (char *)"scottames/ghostty"; argv[5] = NULL;
    (void)osr_run_root(argv);
    argv[0] = (char *)"dnf"; argv[1] = (char *)"install"; argv[2] = (char *)"-y";
    argv[3] = (char *)"ghostty"; argv[4] = NULL;
    rc = osr_run_root(argv);
    if (rc != 0) osr_die("ghostty COPR install failed (exit %d)", rc);
    return 1;
}

/* provide_ghostty_deb -- Debian/Ubuntu community .deb through the
 * ghostty-ubuntu installer (mkasberg/ghostty-ubuntu), which self-detects
 * release and arch and dpkg-installs. Needs bash, and needs to run as root --
 * so this is the `<fetch> | as_root bash` shape, not the as_user one the
 * script: provider uses. */
static int provide_ghostty_deb(void) {
    static const char url[] =
        "https://raw.githubusercontent.com/mkasberg/ghostty-ubuntu/HEAD/install.sh";
    const char *backend;
    char *argv[2];
    int fds[2];
    pid_t dl;
    int rc, status;

    if (!osr_have_cmd("bash")) {
        const char *deps[2];
        deps[0] = "bash"; deps[1] = NULL;
        (void)osr_pkg_install(deps);
    }
    backend = osr_fetch_ensure();
    if (*backend == '\0') osr_die("no downloader found (need curl, wget, or busybox)");
    if (pipe(fds) != 0) osr_die("ghostty-ubuntu install failed (exit 1)");
    dl = osr_fetch_child(backend, url, fds[1]);
    close(fds[1]);
    if (dl < 0) {
        close(fds[0]);
        osr_die("ghostty-ubuntu install failed (exit 1)");
    }
    argv[0] = (char *)"bash"; argv[1] = NULL;
    rc = osr_run_root_in(argv, fds[0]);
    close(fds[0]);
    waitpid(dl, &status, 0);
    /* `$?` after the pipeline is the RIGHT-hand side's, which is what
     * lib/build.sh handed check_error. */
    if (rc != 0) osr_die("ghostty-ubuntu install failed (exit %d)", rc);
    return 1;
}

/* provide_gh_tarball -- GitHub CLI from its release tarball (one static
 * binary), for apt releases with no native `gh` (Debian bullseye). */
static int provide_gh_tarball(void) {
    Str tag, url;
    int ok;

    str_init(&tag); str_init(&url);
    tag_of(&tag, "cli/cli");                                  /* v2.63.0 */
    str_addz(&url, "https://github.com/cli/cli/releases/download/");
    str_addz(&url, str_text(&tag));
    str_addz(&url, "/gh_");
    str_addz(&url, str_text(&tag) + (str_text(&tag)[0] == 'v' ? 1 : 0));   /* 2.63.0 */
    str_addz(&url, "_linux_");
    str_addz(&url, arch_deb());
    str_addz(&url, ".tar.gz");
    ok = osr_install_tarball_bin(str_text(&url), "gh");
    str_free(&tag); str_free(&url);
    return ok;
}

/* provide_btop_tarball -- btop from its static release tarball (bullseye).
 * The asset arch is uname-style. */
static int provide_btop_tarball(void) {
    Str tag, url;
    const char *a = arch();
    int ok;

    str_init(&tag); str_init(&url);
    tag_of(&tag, "aristocratos/btop");                        /* v1.4.0 */
    /* The tag is resolved BEFORE the arch is judged, as in lib/build.sh: an
     * unsupported arch stops the run either way, and keeping the order keeps
     * the two tiers' command logs identical. */
    if (strcmp(a, "x86_64") != 0 && strcmp(a, "aarch64") != 0)
        osr_die("no btop tarball for arch %s", a);
    str_addz(&url, "https://github.com/aristocratos/btop/releases/download/");
    str_addz(&url, str_text(&tag));
    str_addz(&url, "/btop-");
    str_addz(&url, a);
    str_addz(&url, "-unknown-linux-musl.tar.gz");
    ok = osr_install_tarball_bin(str_text(&url), "btop");
    str_free(&tag); str_free(&url);
    return ok;
}

/* provide_lsd_tarball -- the lsd binary from the release .tar.gz (old dpkg). */
static int provide_lsd_tarball(void) {
    Str tag, url;
    const char *a = arch();
    int ok;

    str_init(&tag); str_init(&url);
    tag_of(&tag, "lsd-rs/lsd");                               /* v1.2.0 */
    if (strcmp(a, "x86_64") == 0)        a = "x86_64-unknown-linux-gnu";
    else if (strcmp(a, "aarch64") == 0)  a = "aarch64-unknown-linux-gnu";
    else                                 osr_die("no lsd tarball for arch %s", a);
    str_addz(&url, "https://github.com/lsd-rs/lsd/releases/download/");
    str_addz(&url, str_text(&tag));
    str_addz(&url, "/lsd-");
    str_addz(&url, str_text(&tag));
    str_addc(&url, '-');
    str_addz(&url, a);
    str_addz(&url, ".tar.gz");
    ok = osr_install_tarball_bin(str_text(&url), "lsd");
    str_free(&tag); str_free(&url);
    return ok;
}

/* --- fzf --------------------------------------------------------------------
 * The up-arrow history picker in zsh/rc.d/10-omz.zsh draws itself with
 * --gutter, which landed in fzf 0.66.0. An older fzf does not ignore the flag,
 * it prints `unknown option: --gutter` and exits, so the key answers with an
 * error line instead of a history window. Version, not presence, is the thing
 * to test. */
#define FZF_MIN "0.66"

/* version_2 -- lib/build.sh's
 * `sed -n` script pulling the first MAJOR.MINOR out of a --version line. sed's [^0-9]* cannot step over a digit, so the match only
 * ever comes from the FIRST run of digits in the line, and only when a dot and
 * more digits follow it -- reproduce exactly that, or a line like "fzf 3 (0.74)"
 * would answer here and not there. */
static int version_2(Str *out, const char *line) {
    const char *p = line;
    const char *maj, *min;

    while (*p != '\0' && (*p < '0' || *p > '9')) p++;
    if (*p == '\0') return 0;
    maj = p;
    while (*p >= '0' && *p <= '9') p++;
    if (*p != '.') return 0;
    p++;
    min = p;
    while (*p >= '0' && *p <= '9') p++;
    if (p == min) return 0;
    str_reset(out);
    str_add(out, maj, (size_t)(p - maj));
    return 1;
}

/* tool_version_2 -- MAJOR.MINOR of a tool on PATH, 0 when there is no such
 * tool or its first line carries no version. */
static int tool_version_2(Str *out, const char *tool) {
    Str raw;
    char *argv[3];
    size_t pos = 0;
    Line l;
    int ok = 0;

    if (!osr_have_cmd(tool)) return 0;
    str_init(&raw);
    argv[0] = (char *)tool; argv[1] = (char *)"--version"; argv[2] = NULL;
    (void)osr_run_capture(argv, &raw);
    if (next_line(str_text(&raw), raw.len, &pos, &l)) {
        Str first;
        str_init(&first);
        str_add(&first, l.start, l.len);
        ok = version_2(out, str_text(&first));
        str_free(&first);
    }
    str_free(&raw);
    return ok;
}

/* version_ge -- `have >= want` on MAJOR.MINOR, the numeric comparison the sh
 * builders spelled with two `[ -gt ]` tests. */
static int version_ge(const char *have, const char *want) {
    long hmaj = atol(have), wmaj = atol(want);
    const char *hd = strchr(have, '.'), *wd = strchr(want, '.');
    long hmin = hd != NULL ? atol(hd + 1) : 0;
    long wmin = wd != NULL ? atol(wd + 1) : 0;

    if (hmaj != wmaj) return hmaj > wmaj;
    return hmin >= wmin;
}

/* provide_fzf -- fzf from its upstream release tarball, for the releases whose
 * archive is older than FZF_MIN (the tables in apt.map/dnf.map). One static Go
 * binary per target, so unlike the glibc tarballs above this is also the right
 * route on musl. /usr/local/bin precedes /usr/bin, so the downloaded fzf wins
 * even where the old package stays installed. */
static int provide_fzf(void) {
    Str have, tag, url;
    const char *a = arch();
    int ok;

    str_init(&have);
    if (tool_version_2(&have, "fzf") && version_ge(str_text(&have), FZF_MIN)) {
        /* Asked a second time for the message, because lib/build.sh's line was
         * `info "fzf $(_fzf_version) is already ..."` after its own _fzf_ok:
         * two `fzf --version` runs, and the parity test compares the commands
         * a builder ran, not just the ones that changed something. */
        (void)tool_version_2(&have, "fzf");
        osr_infof("fzf %s is already >= %s - skipping the release binary",
                  str_text(&have), FZF_MIN);
        str_free(&have);
        return 1;
    }
    str_free(&have);

    str_init(&tag); str_init(&url);
    tag_of(&tag, "junegunn/fzf");                             /* v0.74.3 */
    /* fzf's asset arch is Go's (amd64/arm64/armv7), not uname's. */
    if (strcmp(a, "x86_64") == 0)       a = "amd64";
    else if (strcmp(a, "aarch64") == 0) a = "arm64";
    else if (strcmp(a, "armv7l") == 0)  a = "armv7";
    else                                osr_die("no fzf release binary for arch %s", a);

    str_addz(&url, "https://github.com/junegunn/fzf/releases/download/");
    str_addz(&url, str_text(&tag));
    str_addz(&url, "/fzf-");
    str_addz(&url, str_text(&tag) + (str_text(&tag)[0] == 'v' ? 1 : 0));   /* 0.74.3 */
    str_addz(&url, "-linux_");
    str_addz(&url, a);
    str_addz(&url, ".tar.gz");
    ok = osr_install_tarball_bin(str_text(&url), "fzf");
    str_free(&tag); str_free(&url);
    return ok;
}

/* fastfetch_arch -- fastfetch's asset arch naming is mixed (amd64 for x86,
 * aarch64 for arm), and unknown arches fall back to the dpkg name. */
static const char *fastfetch_arch(void) {
    const char *a = arch();
    if (strcmp(a, "x86_64") == 0)  return "amd64";
    if (strcmp(a, "aarch64") == 0) return "aarch64";
    if (strcmp(a, "armv7l") == 0)  return "armv7l";
    return NULL;
}

/* provide_fastfetch_tarball -- the fastfetch binary from the release .tar.gz
 * (old dpkg). */
static int provide_fastfetch_tarball(void) {
    Str tag, url;
    const char *a = fastfetch_arch();
    int ok;

    str_init(&tag); str_init(&url);
    tag_of(&tag, "fastfetch-cli/fastfetch");                  /* 2.66.0 */
    if (a == NULL || strcmp(a, "armv7l") == 0)
        osr_die("no fastfetch tarball for arch %s", arch());
    str_addz(&url, "https://github.com/fastfetch-cli/fastfetch/releases/download/");
    str_addz(&url, str_text(&tag));
    str_addz(&url, "/fastfetch-linux-");
    str_addz(&url, a);
    str_addz(&url, ".tar.gz");
    ok = osr_install_tarball_bin(str_text(&url), "fastfetch");
    str_free(&tag); str_free(&url);
    return ok;
}

/* provide_fastfetch_deb -- fastfetch from its official prebuilt .deb, the
 * "easiest method" on the Debian/Ubuntu releases that do not package it. */
static int provide_fastfetch_deb(void) {
    Str tag, deb, url;
    const char *a = fastfetch_arch();
    int ok;

    if (a == NULL) a = arch_deb();
    str_init(&tag); str_init(&deb); str_init(&url);
    tag_of(&tag, "fastfetch-cli/fastfetch");                  /* 2.66.0, no v */
    str_addz(&deb, "fastfetch-linux-");
    str_addz(&deb, a);
    str_addz(&deb, ".deb");
    str_addz(&url, "https://github.com/fastfetch-cli/fastfetch/releases/download/");
    str_addz(&url, str_text(&tag));
    str_addc(&url, '/');
    str_addz(&url, str_text(&deb));
    ok = install_local_deb(str_text(&url), str_text(&deb), "fastfetch");
    str_free(&tag); str_free(&deb); str_free(&url);
    return ok;
}

/* provide_lsd_deb -- lsd from its official prebuilt .deb, for the Debian/Ubuntu
 * releases too old to ship it natively (jammy). glibc build, not -musl. */
static int provide_lsd_deb(void) {
    Str tag, deb, url;
    int ok;

    str_init(&tag); str_init(&deb); str_init(&url);
    tag_of(&tag, "lsd-rs/lsd");                               /* v1.2.0 */
    str_addz(&deb, "lsd_");
    str_addz(&deb, str_text(&tag) + (str_text(&tag)[0] == 'v' ? 1 : 0));   /* 1.2.0 */
    str_addc(&deb, '_');
    str_addz(&deb, arch_deb());
    str_addz(&deb, ".deb");                                   /* lsd_1.2.0_amd64.deb */
    str_addz(&url, "https://github.com/lsd-rs/lsd/releases/download/");
    str_addz(&url, str_text(&tag));
    str_addc(&url, '/');
    str_addz(&url, str_text(&deb));
    ok = install_local_deb(str_text(&url), str_text(&deb), "lsd");
    str_free(&tag); str_free(&deb); str_free(&url);
    return ok;
}

/* --- the registry ---------------------------------------------------------- */

typedef struct {
    const char *name;
    int (*fn)(void);
} Builder;

static const Builder builders[] = {
    { "provide_yazi_bin",          provide_yazi_bin },
    { "provide_zig",               provide_zig },
    { "provide_ghostty_copr",      provide_ghostty_copr },
    { "provide_ghostty_deb",       provide_ghostty_deb },
    { "provide_gh_tarball",        provide_gh_tarball },
    { "provide_btop_tarball",      provide_btop_tarball },
    { "provide_lsd_tarball",       provide_lsd_tarball },
    { "provide_fzf",               provide_fzf },
    { "provide_fastfetch_tarball", provide_fastfetch_tarball },
    { "provide_fastfetch_deb",     provide_fastfetch_deb },
    { "provide_lsd_deb",           provide_lsd_deb }
};
#define BUILDER_COUNT (sizeof(builders) / sizeof(builders[0]))

static const Builder *lookup(const char *fn) {
    size_t i;
    for (i = 0; i < BUILDER_COUNT; i++) {
        if (strcmp(builders[i].name, fn) == 0) return &builders[i];
    }
    return NULL;
}

int osr_build_has(const char *fn) { return lookup(fn) != NULL; }

int osr_build_run(const char *fn) {
    const Builder *b = lookup(fn);
    if (b == NULL) return 0;
    return b->fn();
}

/* --- the command ----------------------------------------------------------- */

static int build_usage(void) {
    fputs("usage: osr build <subcommand> [args]\n\n", stderr);
    fputs("  list             the builders that have been ported to C\n", stderr);
    fputs("  has <fn>         whether this builder runs here or in lib/build.sh\n", stderr);
    fputs("  run <fn>         run it\n", stderr);
    return 2;
}

int osr_build_main(int argc, char **argv) {
    size_t i;

    if (argc < 2) return build_usage();
    if (strcmp(argv[1], "list") == 0 && argc == 2) {
        for (i = 0; i < BUILDER_COUNT; i++) printf("%s\n", builders[i].name);
        return 0;
    }
    if (strcmp(argv[1], "has") == 0 && argc == 3) return osr_build_has(argv[2]) ? 0 : 1;
    if (strcmp(argv[1], "run") == 0 && argc == 3) {
        if (!osr_build_has(argv[2])) {
            fprintf(stderr, "osr build: '%s' is still a lib/build.sh function\n", argv[2]);
            return 1;
        }
        return osr_build_run(argv[2]) ? 0 : 1;
    }
    return build_usage();
}
