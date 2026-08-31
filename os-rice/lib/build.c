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
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
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

/* ends_with -- does path end in suffix? `find -path '*<suffix>'`. */
static int ends_with(const char *path, const char *suffix) {
    size_t p = strlen(path), q = strlen(suffix);
    return p >= q && strcmp(path + (p - q), suffix) == 0;
}

/* find_walk -- the one walk behind every `find ... | head -n 1` in lib/build.sh.
 * name matches a basename, suffix matches the tail of the whole path, and depth
 * is find's -maxdepth counted from dir. Exactly one of name/suffix is given.
 * Returns 0 when nothing below dir matches. */
static int find_walk(Str *out, const char *dir, const char *name,
                     const char *suffix, int depth) {
    DIR *d;
    struct dirent *e;
    int found = 0;

    if (depth <= 0) return 0;
    d = opendir(dir);
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
                found = find_walk(out, path.p, name, suffix, depth - 1);
            } else if (S_ISREG(st.st_mode) &&
                       (name != NULL ? strcmp(e->d_name, name) == 0
                                     : ends_with(str_text(&path), suffix))) {
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

/* find_file -- `find <dir> -type f -name <name> | head -n 1`. */
static int find_file(Str *out, const char *dir, const char *name) {
    return find_walk(out, dir, name, NULL, 64);
}

/* find_file_depth -- the same with find's -maxdepth. */
static int find_file_depth(Str *out, const char *dir, const char *name, int depth) {
    return find_walk(out, dir, name, NULL, depth);
}

/* find_path -- `find <dir> -type f -path '*<suffix>' | head -n 1`. */
static int find_path(Str *out, const char *dir, const char *suffix) {
    return find_walk(out, dir, NULL, suffix, 64);
}

/* first_subdir -- `find <dir> -mindepth 1 -maxdepth 1 -type d | head -n 1`: the
 * single versioned directory a vendor tarball unpacks into. */
static int first_subdir(Str *out, const char *dir) {
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
        if (lstat(path.p, &st) == 0 && S_ISDIR(st.st_mode)) {
            str_reset(out);
            str_addz(out, path.p);
            found = 1;
        }
        str_free(&path);
    }
    closedir(d);
    return found;
}

/* make_tmp_file -- `mktemp`, for the builders that stage ONE file. */
static int make_tmp_file(Str *out) {
    Str tpl;
    int fd;

    str_init(&tpl);
    str_addz(&tpl, tmp_root());
    str_addz(&tpl, "/tmp.XXXXXX");
    fd = mkstemp(tpl.p);
    if (fd < 0) { str_free(&tpl); return 0; }
    close(fd);
    str_reset(out);
    str_addz(out, tpl.p);
    str_free(&tpl);
    return 1;
}

/* refresh_desktop_db -- `command -v update-desktop-database && as_root
 * update-desktop-database /usr/share/applications >/dev/null 2>&1 || :` -- the
 * best-effort tail of every builder that writes a .desktop entry. */
static void refresh_desktop_db(void) {
    char *argv[3];
    if (!osr_have_cmd("update-desktop-database")) return;
    argv[0] = (char *)"update-desktop-database";
    argv[1] = (char *)"/usr/share/applications";
    argv[2] = NULL;
    (void)osr_run_root_quiet(argv);
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

/* pkgconfig_env -- _osr_pkgconfig_path, as the `PKG_CONFIG_PATH=<value>`
 * assignment the sh builders handed to `env`.
 *
 * A pkg-config from Homebrew/conda/nix shadows the system one on PATH and
 * searches ONLY its own prefix, so a source build fails on system libs that ARE
 * installed (wayland-client for wezterm, gtk4 for ghostty). Appending the
 * standard dirs costs nothing when the system pkg-config is in use: they are
 * already its defaults, and dirs that do not exist are ignored. */
static void pkgconfig_env(Str *out) {
    const char *had = env_str("PKG_CONFIG_PATH", "");
    struct utsname u;

    str_reset(out);
    str_addz(out, "PKG_CONFIG_PATH=");
    if (*had != '\0') {
        str_addz(out, had);
        str_addc(out, ':');
    }
    str_addz(out, "/usr/lib/");
    str_addz(out, uname(&u) == 0 ? u.machine : "");     /* `uname -m` */
    str_addz(out, "-linux-gnu/pkgconfig:/usr/lib64/pkgconfig:/usr/lib/pkgconfig"
                  ":/usr/share/pkgconfig");
}

/* build_jobs -- `${OSR_BUILD_JOBS:-$(nproc 2>/dev/null || echo 2)}`. nproc is
 * FORKED rather than replaced with sysconf(): it honours the cpu affinity mask
 * and sysconf does not, and this number goes into a `make -j<n>` the parity
 * test compares -- two tiers must not disagree about it on a pinned box. */
static void build_jobs(Str *out) {
    const char *pinned = env_str("OSR_BUILD_JOBS", "");
    char *argv[2];
    Str raw;

    str_reset(out);
    if (*pinned != '\0') {
        str_addz(out, pinned);
        return;
    }
    str_init(&raw);
    argv[0] = (char *)"nproc"; argv[1] = NULL;
    if (osr_run_capture(argv, &raw)) {
        str_trim_trailing(&raw, '\n');
        if (raw.len > 0) str_addz(out, str_text(&raw));
    }
    if (out->len == 0) str_addz(out, "2");
    str_free(&raw);
}

/* jobs_flag -- the same number as one `-j<n>` argument, which is how the sh
 * builders spelled it (`make -j"$(...)"`, one word). */
static void jobs_flag(Str *out) {
    Str n;
    str_init(&n);
    build_jobs(&n);
    str_reset(out);
    str_addz(out, "-j");
    str_addz(out, str_text(&n));
    str_free(&n);
}

/* Who a command runs as, which for the compiling builders is not always the
 * caller: a build runs as OSR_USER, its install as root (§8). */
typedef enum { AS_SELF, AS_ROOT, AS_USER } As;

/* run_in_dir -- `( cd <dir> && [as_root|as_user] <argv> )`. The subshell the sh
 * builders opened exists only to keep the chdir local, so that is all this
 * does: fork, chdir, run, report the status. */
static int run_in_dir(const char *dir, As as, char *const argv[]) {
    pid_t pid;
    int status;

    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid < 0) return 127;
    if (pid == 0) {
        int rc;
        if (chdir(dir) != 0) _exit(127);
        rc = as == AS_ROOT ? osr_run_root(argv)
           : as == AS_USER ? osr_run_user(argv)
           : osr_run(argv);
        _exit(rc);
    }
    if (waitpid(pid, &status, 0) < 0) return 127;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

/* pkg -- `pkg_install <names...>` for a builder's own prerequisites (§1a). */
static void pkg(const char *const names[]) { (void)osr_pkg_install(names); }

/* --- version probes, for the builders whose idempotency is a VERSION -------
 * Two builders (fzf, chafa) are not satisfied by `command -v <name>`: an older
 * build of either is present, answers the probe, and still cannot do the one
 * thing it was installed for. Both spelled the check the same way in sh, so it
 * is one set of helpers here.
 */

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

/* --- the shared primitives ------------------------------------------------- */

int osr_install_tarball_bin(const char *url, const char *bin) {
    Str tmp, tar_path, found, dest;
    char *argv[7];
    int ok;

    if (osr_theme_only()) return osr_theme_only_skip("_osr_install_tarball_bin");

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

/* --- chafa -------------------------------------------------------------------
 * CHAFA_MIN is the chafa yazi actually needs, and the reason this builder
 * exists. Yazi drives the adapter as `chafa -f symbols --relative off --probe
 * off ...`, and --probe landed in chafa 1.16.0. An older chafa exits on the
 * unrecognized option, so the preview pane just stays blank -- no error in the
 * UI, nothing in the log. Version, not presence, is the thing to test. */
#define CHAFA_MIN OSR_CHAFA_MIN

/* osr_chafa_ok -- _chafa_ok: is the chafa on PATH new enough for yazi? */
int osr_chafa_ok(void) {
    Str have;
    int ok;
    str_init(&have);
    ok = tool_version_2(&have, "chafa") && version_ge(str_text(&have), CHAFA_MIN);
    str_free(&have);
    return ok;
}

/* provide_chafa -- build chafa from the upstream release tarball, for the
 * targets whose archive is older than CHAFA_MIN (the table in apt.map).
 * Upstream ships a SOURCE tarball only -- no prebuilt Linux binary anywhere --
 * so those releases have to compile, unlike every other builder here. It is a
 * small C project: glib + freetype are the only mandatory deps, PNG/GIF decode
 * is built in, and jpeg/webp/tiff come from the optional loaders in
 * chafa-build-deps so the formats a file manager actually previews all render.
 *
 * Idempotency goes BEYOND the caller's `command -v chafa` probe (§2): presence
 * is not sufficiency here. That also makes it safe to call directly, which
 * modules/yazi.sh does to repair a box that already had an old distro chafa --
 * that one satisfies the probe and would otherwise never be replaced.
 * /usr/local/bin precedes /usr/bin, so the built chafa wins even where the old
 * package stays installed. */
static int provide_chafa(void) {
    static const char *const deps[] = { "build", "chafa-build-deps", "tar", "xz", NULL };
    Str have, ver, tmp, tar_path, src, pc, jobs;
    char *argv[6];

    str_init(&have);
    if (tool_version_2(&have, "chafa") && version_ge(str_text(&have), CHAFA_MIN)) {
        /* Asked a second time for the message, as lib/build.sh's line was
         * `info "chafa $(_chafa_version) is already ..."` after its own
         * _chafa_ok -- two `chafa --version` runs, and the parity test compares
         * the commands a builder ran, not just the ones that changed something. */
        (void)tool_version_2(&have, "chafa");
        osr_infof("chafa %s is already >= %s - skipping the source build",
                  str_text(&have), CHAFA_MIN);
        str_free(&have);
        return 1;
    }
    str_free(&have);
    pkg(deps);

    str_init(&ver);
    tag_of(&ver, "hpjansson/chafa");
    if (str_text(&ver)[0] == 'v') {                    /* tags carry no v */
        Str bare;
        str_init(&bare);
        str_addz(&bare, str_text(&ver) + 1);
        str_reset(&ver);
        str_addz(&ver, str_text(&bare));
        str_free(&bare);
    }

    str_init(&tmp); str_init(&tar_path); str_init(&src); str_init(&pc); str_init(&jobs);
    if (!make_tmp_dir(&tmp)) osr_die("failed to create a temporary directory");
    str_addz(&tar_path, str_text(&tmp));
    str_addz(&tar_path, "/chafa.tar.xz");
    {
        Str url;
        str_init(&url);
        str_addz(&url, "https://github.com/hpjansson/chafa/releases/download/");
        str_addz(&url, str_text(&ver));
        str_addz(&url, "/chafa-");
        str_addz(&url, str_text(&ver));
        str_addz(&url, ".tar.xz");
        if (!osr_fetch_download(str_text(&url), tar_path.p, 0)) {
            rm_rf(str_text(&tmp));
            osr_die("failed to download chafa %s", str_text(&ver));
        }
        str_free(&url);
    }
    argv[0] = (char *)"tar"; argv[1] = (char *)"-xf"; argv[2] = tar_path.p;
    argv[3] = (char *)"-C"; argv[4] = tmp.p; argv[5] = NULL;
    if (!run_ok(argv)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to extract chafa %s", str_text(&ver));
    }
    str_addz(&src, str_text(&tmp));
    str_addz(&src, "/chafa-");
    str_addz(&src, str_text(&ver));

    /* The dist tarball is pre-autotooled -- ./configure is already generated,
     * so no autogen.sh run and no autoconf/automake/libtool in the dep list. */
    pkgconfig_env(&pc);
    jobs_flag(&jobs);
    argv[0] = (char *)"env"; argv[1] = pc.p; argv[2] = (char *)"./configure";
    argv[3] = (char *)"--prefix=/usr/local"; argv[4] = NULL;
    if (run_in_dir(str_text(&src), AS_SELF, argv) != 0) {
        rm_rf(str_text(&tmp));
        osr_die("chafa build failed");
    }
    argv[0] = (char *)"make"; argv[1] = jobs.p; argv[2] = NULL;
    if (run_in_dir(str_text(&src), AS_SELF, argv) != 0) {
        rm_rf(str_text(&tmp));
        osr_die("chafa build failed");
    }
    argv[0] = (char *)"make"; argv[1] = (char *)"-C"; argv[2] = src.p;
    argv[3] = (char *)"install"; argv[4] = NULL;
    if (osr_run_root(argv) != 0) {
        rm_rf(str_text(&tmp));
        osr_die("chafa install failed");
    }
    /* libchafa lands in /usr/local/lib; refresh the loader cache so the binary
     * finds it on distros that do not scan that dir by default. */
    argv[0] = (char *)"ldconfig"; argv[1] = NULL;
    (void)osr_run_root_quiet(argv);
    rm_rf(str_text(&tmp));

    str_free(&ver); str_free(&tmp); str_free(&tar_path);
    str_free(&src); str_free(&pc); str_free(&jobs);
    return 1;
}

/* provide_ueberzugpp -- build Uberzug++ from the upstream release tarball, for
 * the targets that package it nowhere: Arch, Gentoo, openSUSE and NixOS carry
 * it, Debian/Ubuntu/Fedora/Void/Alpine carry nothing. It is what yazi actually
 * uses on a graphical session, so on those distros there is no packaged route
 * to a working image preview at all.
 *
 * Upstream installs BOTH names: the binary `ueberzug` plus a symlink
 * `ueberzugpp`. That symlink is the one that matters, because yazi spawns
 * exactly `ueberzugpp layer -so <driver>` -- hence the check at the end.
 *
 * -DENABLE_OPENCV=OFF picks libvips for image loading instead, which is the far
 * lighter dep tree. X11 and Wayland outputs are both compiled in so one recipe
 * covers every session; the gate in modules/yazi.sh decides whether to build at
 * all, not which. */
static int provide_ueberzugpp(void) {
    static const char *const deps[] = { "build", "cmake", "ueberzugpp-build-deps", NULL };
    Str tag, ver, tmp, tar_path, src, bld, pc, jobs, cml;
    char *argv[10];

    pkg(deps);
    str_init(&tag);
    tag_of(&tag, "jstkdng/ueberzugpp");                /* v2.9.10 */
    str_init(&ver);
    str_addz(&ver, str_text(&tag) + (str_text(&tag)[0] == 'v' ? 1 : 0));

    str_init(&tmp); str_init(&tar_path); str_init(&src); str_init(&bld);
    str_init(&pc); str_init(&jobs); str_init(&cml);
    if (!make_tmp_dir(&tmp)) osr_die("failed to create a temporary directory");
    str_addz(&tar_path, str_text(&tmp));
    str_addz(&tar_path, "/ueberzugpp.tar.gz");
    {
        Str url;
        str_init(&url);
        str_addz(&url, "https://github.com/jstkdng/ueberzugpp/archive/refs/tags/");
        str_addz(&url, str_text(&tag));
        str_addz(&url, ".tar.gz");
        if (!osr_fetch_download(str_text(&url), tar_path.p, 0)) {
            rm_rf(str_text(&tmp));
            osr_die("failed to download ueberzugpp %s", str_text(&tag));
        }
        str_free(&url);
    }
    argv[0] = (char *)"tar"; argv[1] = (char *)"-xf"; argv[2] = tar_path.p;
    argv[3] = (char *)"-C"; argv[4] = tmp.p; argv[5] = NULL;
    if (!run_ok(argv)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to extract ueberzugpp %s", str_text(&tag));
    }
    str_addz(&src, str_text(&tmp));
    str_addz(&src, "/ueberzugpp-");
    str_addz(&src, str_text(&ver));
    str_addz(&cml, str_text(&src));
    str_addz(&cml, "/CMakeLists.txt");
    if (!file_exists(str_text(&cml))) {
        rm_rf(str_text(&tmp));
        osr_die("no CMakeLists.txt in the ueberzugpp tarball - its layout changed");
    }
    str_addz(&bld, str_text(&src));
    str_addz(&bld, "/build");

    pkgconfig_env(&pc);
    argv[0] = (char *)"env"; argv[1] = pc.p; argv[2] = (char *)"cmake";
    argv[3] = (char *)"-S"; argv[4] = src.p; argv[5] = (char *)"-B"; argv[6] = bld.p;
    argv[7] = NULL;
    {
        char *full[13];
        size_t n = 0;
        for (n = 0; argv[n] != NULL; n++) full[n] = argv[n];
        full[n++] = (char *)"-DCMAKE_BUILD_TYPE=Release";
        full[n++] = (char *)"-DCMAKE_INSTALL_PREFIX=/usr/local";
        full[n++] = (char *)"-DENABLE_OPENCV=OFF";
        full[n++] = (char *)"-DENABLE_X11=ON";
        full[n++] = (char *)"-DENABLE_WAYLAND=ON";
        full[n] = NULL;
        if (osr_run(full) != 0) {
            rm_rf(str_text(&tmp));
            osr_die("ueberzugpp cmake configure failed");
        }
    }
    build_jobs(&jobs);
    {
        Str par;
        str_init(&par);
        str_addz(&par, "CMAKE_BUILD_PARALLEL_LEVEL=");
        str_addz(&par, str_text(&jobs));
        argv[0] = (char *)"env"; argv[1] = par.p; argv[2] = (char *)"cmake";
        argv[3] = (char *)"--build"; argv[4] = bld.p; argv[5] = NULL;
        if (osr_run(argv) != 0) {
            str_free(&par);
            rm_rf(str_text(&tmp));
            osr_die("ueberzugpp build failed");
        }
        str_free(&par);
    }
    argv[0] = (char *)"cmake"; argv[1] = (char *)"--install"; argv[2] = bld.p; argv[3] = NULL;
    if (osr_run_root(argv) != 0) {
        rm_rf(str_text(&tmp));
        osr_die("ueberzugpp install failed");
    }
    argv[0] = (char *)"ldconfig"; argv[1] = NULL;
    (void)osr_run_root_quiet(argv);
    rm_rf(str_text(&tmp));

    str_free(&tag); str_free(&ver); str_free(&tmp); str_free(&tar_path);
    str_free(&src); str_free(&bld); str_free(&pc); str_free(&jobs); str_free(&cml);
    if (!osr_have_cmd("ueberzugpp"))
        osr_die("ueberzugpp installed but not on PATH - yazi spawns it by that exact name");
    return 1;
}

/* provide_paru -- bootstrap the paru AUR helper from the AUR. The chicken/egg
 * package: the one AUR package that cannot come FROM an AUR helper. Clone its
 * PKGBUILD and makepkg it as OSR_USER (makepkg refuses root); every later `aur:`
 * row then dispatches through paru. Arch-only. Idempotency is the caller's
 * `command -v paru` probe, so a rerun with paru present is a no-op. */
static int provide_paru(void) {
    static const char *const deps[] = { "build", "git", NULL };   /* base-devel + git */
    Str repo;
    char *argv[7];

    pkg(deps);
    str_init(&repo);
    str_addz(&repo, tmp_root());
    str_addz(&repo, "/osr-paru-build");

    argv[0] = (char *)"rm"; argv[1] = (char *)"-rf"; argv[2] = repo.p; argv[3] = NULL;
    (void)osr_run_user(argv);
    argv[0] = (char *)"git"; argv[1] = (char *)"clone"; argv[2] = (char *)"--depth";
    argv[3] = (char *)"1"; argv[4] = (char *)"https://aur.archlinux.org/paru.git";
    argv[5] = repo.p; argv[6] = NULL;
    if (osr_run_user(argv) != 0) osr_die("failed to clone paru AUR repo");

    argv[0] = (char *)"makepkg"; argv[1] = (char *)"-si"; argv[2] = (char *)"--needed";
    argv[3] = (char *)"--noconfirm"; argv[4] = NULL;
    if (run_in_dir(str_text(&repo), AS_USER, argv) != 0) {
        argv[0] = (char *)"rm"; argv[1] = (char *)"-rf"; argv[2] = repo.p; argv[3] = NULL;
        (void)osr_run_user(argv);
        osr_die("paru build failed");
    }
    argv[0] = (char *)"rm"; argv[1] = (char *)"-rf"; argv[2] = repo.p; argv[3] = NULL;
    (void)osr_run_user(argv);
    str_free(&repo);
    return 1;
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

    if (osr_theme_only()) return osr_theme_only_skip("provide_zig");

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

/* provide_ghostty -- build the Ghostty terminal from source with Zig; the
 * fallback for targets with no native package and no community binary (older
 * Debian/Ubuntu, Alpine/musl). It reads the exact Zig version Ghostty pins from
 * its own source tree and installs THAT one (G1: a source: builder with a
 * bootstrapped toolchain prerequisite under it). Heavy -- a full Zig compile --
 * so a real-desktop concern, §9, not part of the container matrix. */
static int provide_ghostty(void) {
    /* GTK/build deps as logical names; pkgmap splits them per distro. */
    static const char *const deps[] = {
        "build", "gtk4-dev", "libadwaita-dev", "gettext", "pkg-config", "tar", "xz", NULL
    };
    Str ver, tmp, tar_path, src, zigver, pc;
    char *argv[7];
    char *pinned;
    size_t len;

    pkg(deps);
    str_init(&ver);
    tag_of(&ver, "ghostty-org/ghostty");
    if (str_text(&ver)[0] == 'v') {
        Str bare;
        str_init(&bare);
        str_addz(&bare, str_text(&ver) + 1);
        str_reset(&ver);
        str_addz(&ver, str_text(&bare));
        str_free(&bare);
    }

    str_init(&tmp); str_init(&tar_path); str_init(&src); str_init(&zigver); str_init(&pc);
    if (!make_tmp_dir(&tmp)) osr_die("failed to create a temporary directory");
    str_addz(&tar_path, str_text(&tmp));
    str_addz(&tar_path, "/ghostty.tar.gz");
    {
        Str url;
        str_init(&url);
        str_addz(&url, "https://release.files.ghostty.org/");
        str_addz(&url, str_text(&ver));
        str_addz(&url, "/ghostty-");
        str_addz(&url, str_text(&ver));
        str_addz(&url, ".tar.gz");
        if (!osr_fetch_download(str_text(&url), tar_path.p, 0)) {
            rm_rf(str_text(&tmp));
            osr_die("failed to download ghostty %s", str_text(&ver));
        }
        str_free(&url);
    }
    argv[0] = (char *)"tar"; argv[1] = (char *)"-xf"; argv[2] = tar_path.p;
    argv[3] = (char *)"-C"; argv[4] = tmp.p; argv[5] = NULL;
    if (!run_ok(argv)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to extract ghostty");
    }
    str_addz(&src, str_text(&tmp));
    str_addz(&src, "/ghostty-");
    str_addz(&src, str_text(&ver));

    /* `cat .zig-version | tr -d '[:space:]'`: the file is one line, and an
     * absent one leaves the version empty, which osr_build_zig reads as
     * "newest stable" -- the same thing the sh builder did with it. */
    {
        Str vpath;
        str_init(&vpath);
        str_addz(&vpath, str_text(&src));
        str_addz(&vpath, "/.zig-version");
        pinned = slurp(str_text(&vpath), &len);
        str_free(&vpath);
    }
    if (pinned != NULL) {
        size_t i;
        for (i = 0; i < len; i++) {
            if (!is_space(pinned[i])) str_addc(&zigver, pinned[i]);
        }
        free(pinned);
    }
    if (!osr_build_zig(str_text(&zigver))) {
        rm_rf(str_text(&tmp));
        osr_die("failed to install the zig ghostty pins");
    }

    pkgconfig_env(&pc);
    argv[0] = (char *)"env"; argv[1] = pc.p; argv[2] = (char *)"zig";
    argv[3] = (char *)"build"; argv[4] = (char *)"-p"; argv[5] = (char *)"/usr";
    argv[6] = NULL;
    {
        char *full[9];
        size_t n;
        for (n = 0; argv[n] != NULL; n++) full[n] = argv[n];
        full[n++] = (char *)"-Doptimize=ReleaseFast";
        full[n] = NULL;
        if (run_in_dir(str_text(&src), AS_ROOT, full) != 0) {
            rm_rf(str_text(&tmp));
            osr_die("ghostty build failed");
        }
    }
    rm_rf(str_text(&tmp));
    str_free(&ver); str_free(&tmp); str_free(&tar_path);
    str_free(&src); str_free(&zigver); str_free(&pc);
    return 1;
}

/* provide_wezterm -- build WezTerm from source, the route upstream documents.
 * No AppImage/flatpak: the source build is the ONLY install method here, so
 * every distro gets the same binary from the same recipe. Heavy (a full Rust
 * workspace compile) -- a real-desktop concern, §9.
 *
 * Needs a toolchain: list `rust` BEFORE `wezterm` in the rice (manifest order is
 * the dependency graph, §4). Upstream's own build deps come from the repo's
 * ./get-deps, which detects the distro and installs them -- one script instead
 * of a per-distro dep list duplicated into every pkgmap. */
static int provide_wezterm(void) {
    static const char *const deps[] = { "build", "git", NULL };
    static const char *const bins[] = { "wezterm", "wezterm-gui", "wezterm-mux-server", NULL };
    Str cargo, src, cargo_tmpl, pc, path_env, rustup, cargo_home, from, to;
    char *argv[9];
    size_t i;

    pkg(deps);
    str_init(&cargo);
    str_addz(&cargo, osr_mod_home());
    str_addz(&cargo, "/.cargo/bin/cargo");
    argv[0] = (char *)"test"; argv[1] = (char *)"-x"; argv[2] = cargo.p; argv[3] = NULL;
    if (osr_run_user(argv) != 0)
        osr_die("cargo not found - install 'rust' before wezterm (manifest order, section 4)");

    /* A failed build KEEPS the checkout, so a retry reuses it: no second
     * 15-minute compile from scratch after a transient registry/network blip.
     * Only a successful install cleans it up. */
    str_init(&src);
    str_addz(&src, tmp_root());
    str_addz(&src, "/osr-wezterm-src");
    str_init(&cargo_tmpl);
    str_addz(&cargo_tmpl, str_text(&src));
    str_addz(&cargo_tmpl, "/Cargo.toml");
    if (file_exists(str_text(&cargo_tmpl))) {
        osr_infof("reusing the existing wezterm checkout (%s) - rebuild is incremental",
                  str_text(&src));
    } else {
        argv[0] = (char *)"rm"; argv[1] = (char *)"-rf"; argv[2] = src.p; argv[3] = NULL;
        (void)osr_run_user(argv);
        /* --branch=main is what upstream documents for a source build; the
         * submodules (freetype/harfbuzz/... vendored deps) are not optional. */
        argv[0] = (char *)"git"; argv[1] = (char *)"clone"; argv[2] = (char *)"--depth=1";
        argv[3] = (char *)"--branch=main"; argv[4] = (char *)"--recursive";
        argv[5] = (char *)"https://github.com/wezterm/wezterm.git";
        argv[6] = src.p; argv[7] = NULL;
        if (osr_run_user(argv) != 0) osr_die("failed to clone wezterm");
    }
    argv[0] = (char *)"git"; argv[1] = (char *)"submodule"; argv[2] = (char *)"update";
    argv[3] = (char *)"--init"; argv[4] = (char *)"--recursive"; argv[5] = NULL;
    if (run_in_dir(str_text(&src), AS_USER, argv) != 0)
        osr_die("wezterm submodule checkout failed");

    /* get-deps needs root to install, but ends with a `rustc --version` check --
     * and the toolchain is OSR_USER's, not root's (§8 user-for-user). sudo
     * resets PATH, so root does not see ~/.cargo/bin; and the cargo/rustc SHIMS
     * resolve the actual toolchain through RUSTUP_HOME, which for root is
     * /root/.rustup (empty -> "could not choose a version of rustc to run").
     * Point all three at OSR_USER's toolchain, or get-deps exits 1 on a box
     * where the deps installed fine. */
    str_init(&path_env); str_init(&rustup); str_init(&cargo_home);
    str_addz(&path_env, "PATH=");
    str_addz(&path_env, osr_mod_home());
    str_addz(&path_env, "/.cargo/bin:");
    str_addz(&path_env, env_str("PATH", ""));
    str_addz(&rustup, "RUSTUP_HOME=");
    str_addz(&rustup, osr_mod_home());
    str_addz(&rustup, "/.rustup");
    str_addz(&cargo_home, "CARGO_HOME=");
    str_addz(&cargo_home, osr_mod_home());
    str_addz(&cargo_home, "/.cargo");
    argv[0] = (char *)"env"; argv[1] = path_env.p; argv[2] = rustup.p;
    argv[3] = cargo_home.p; argv[4] = (char *)"./get-deps"; argv[5] = NULL;
    if (run_in_dir(str_text(&src), AS_ROOT, argv) != 0) osr_die("wezterm get-deps failed");

    /* PKG_CONFIG_PATH: see pkgconfig_env -- a shadowing pkg-config (brew et al)
     * otherwise fails the wayland-sys build script on a box that HAS the libs. */
    str_init(&pc);
    pkgconfig_env(&pc);
    argv[0] = (char *)"env"; argv[1] = pc.p; argv[2] = cargo.p;
    argv[3] = (char *)"build"; argv[4] = (char *)"--release"; argv[5] = NULL;
    if (run_in_dir(str_text(&src), AS_USER, argv) != 0)
        osr_die("wezterm build failed (checkout kept at %s - rerun to resume)", str_text(&src));

    str_init(&from); str_init(&to);
    for (i = 0; bins[i] != NULL; i++) {
        str_reset(&from);
        str_addz(&from, str_text(&src));
        str_addz(&from, "/target/release/");
        str_addz(&from, bins[i]);
        str_reset(&to);
        str_addz(&to, "/usr/local/bin/");
        str_addz(&to, bins[i]);
        argv[0] = (char *)"install"; argv[1] = (char *)"-m"; argv[2] = (char *)"0755";
        argv[3] = from.p; argv[4] = to.p; argv[5] = NULL;
        if (osr_run_root(argv) != 0) osr_die("failed to install %s", bins[i]);
    }

    /* Desktop entry + icon so a DE launcher finds it. Cosmetic: warn, never fail. */
    str_reset(&from);
    str_addz(&from, str_text(&src));
    str_addz(&from, "/assets/wezterm.desktop");
    if (file_exists(str_text(&from))) {
        argv[0] = (char *)"install"; argv[1] = (char *)"-Dm"; argv[2] = (char *)"0644";
        argv[3] = from.p;
        argv[4] = (char *)"/usr/local/share/applications/org.wezfurlong.wezterm.desktop";
        argv[5] = NULL;
        if (osr_run_root(argv) != 0) osr_warn("failed to install the wezterm desktop entry");
        str_reset(&from);
        str_addz(&from, str_text(&src));
        str_addz(&from, "/assets/icon/terminal.png");
        argv[3] = from.p;
        argv[4] = (char *)"/usr/local/share/icons/hicolor/128x128/apps/org.wezfurlong.wezterm.png";
        if (osr_run_root(argv) != 0) osr_warn("failed to install the wezterm icon");
    }
    argv[0] = (char *)"rm"; argv[1] = (char *)"-rf"; argv[2] = src.p; argv[3] = NULL;
    (void)osr_run_user(argv);

    str_free(&cargo); str_free(&src); str_free(&cargo_tmpl); str_free(&pc);
    str_free(&path_env); str_free(&rustup); str_free(&cargo_home);
    str_free(&from); str_free(&to);
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
#define FZF_MIN OSR_FZF_MIN

/* osr_fzf_ok -- _fzf_ok: is the fzf on PATH new enough for the ↑ history picker? */
int osr_fzf_ok(void) {
    Str have;
    int ok;
    str_init(&have);
    ok = tool_version_2(&have, "fzf") && version_ge(str_text(&have), FZF_MIN);
    str_free(&have);
    return ok;
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

/* --- the vendor-tarball and script builders ---------------------------------
 * What these have in common is that nothing is compiled: a tree or a script is
 * fetched from a vendor and put in place. What differs is WHO ends up owning
 * it, and that is never incidental -- see provide_telegram.
 */

/* provide_thunderbird_tarball -- Thunderbird from Mozilla's official Linux
 * tarball, installed as a tree under /opt with a symlink and a .desktop entry.
 * The Debian/Ubuntu route, for two reasons the archive cannot fix:
 *
 *   snap  On Ubuntu 24.04+ the archive's `thunderbird` is a transitional stub
 *         whose only job is `snap install thunderbird`. The snap relocates the
 *         profile root to ~/snap/thunderbird/common/.thunderbird, so the §5/§6
 *         layer modules/thunderbird.sh writes to ~/.thunderbird is read by
 *         nothing.
 *   ESR   Debian pins an ESR (91/115/128 on bullseye..trixie). Native
 *         Exchange/EWS accounts need Thunderbird 140+.
 *
 * Not the Mozilla APT repo: packages.mozilla.org carries firefox only. The
 * tarball is x86_64-only, so other arches get a clear error rather than a
 * mystery 404. */
static const char thunderbird_desktop[] =
    "[Desktop Entry]\n"
    "Name=Thunderbird\n"
    "Comment=Send and receive mail\n"
    "Exec=/opt/thunderbird/thunderbird %u\n"
    "Icon=/opt/thunderbird/chrome/icons/default/default128.png\n"
    "Terminal=false\n"
    "Type=Application\n"
    "Categories=Network;Email;\n"
    "MimeType=x-scheme-handler/mailto;message/rfc822;\n"
    "StartupNotify=true\n"
    "StartupWMClass=thunderbird\n";

static int provide_thunderbird_tarball(void) {
    static const char *const deps[] = { "tar", "xz", NULL };
    Str tmp, tar_path, unpacked;
    char *argv[6];

    if (strcmp(arch(), "x86_64") != 0)
        osr_die("Mozilla publishes no Linux %s Thunderbird build - use the distro "
                "package (an ESR without Exchange/EWS) on this arch", arch());
    pkg(deps);

    str_init(&tmp); str_init(&tar_path); str_init(&unpacked);
    if (!make_tmp_dir(&tmp)) osr_die("failed to create a temporary directory");
    str_addz(&tar_path, str_text(&tmp));
    str_addz(&tar_path, "/thunderbird.tar.xz");
    osr_info("downloading the latest Thunderbird from download.mozilla.org");
    if (!osr_fetch_download(
            "https://download.mozilla.org/?product=thunderbird-latest&os=linux64&lang=en-US",
            tar_path.p, 0)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to download Thunderbird");
    }
    argv[0] = (char *)"tar"; argv[1] = (char *)"-xf"; argv[2] = tar_path.p;
    argv[3] = (char *)"-C"; argv[4] = tmp.p; argv[5] = NULL;
    if (!run_ok(argv)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to extract the Thunderbird tarball");
    }
    str_addz(&unpacked, str_text(&tmp));
    str_addz(&unpacked, "/thunderbird");
    {
        Str binary;
        int ok;
        str_init(&binary);
        str_addz(&binary, str_text(&unpacked));
        str_addz(&binary, "/thunderbird");
        ok = access(str_text(&binary), X_OK) == 0;
        str_free(&binary);
        if (!ok) {
            rm_rf(str_text(&tmp));
            osr_die("no thunderbird binary in the tarball");
        }
    }
    argv[0] = (char *)"rm"; argv[1] = (char *)"-rf";
    argv[2] = (char *)"/opt/thunderbird"; argv[3] = NULL;
    (void)osr_run_root(argv);
    argv[0] = (char *)"mv"; argv[1] = unpacked.p;
    argv[2] = (char *)"/opt/thunderbird"; argv[3] = NULL;
    if (osr_run_root(argv) != 0) {
        rm_rf(str_text(&tmp));
        osr_die("failed to install Thunderbird into /opt");
    }
    rm_rf(str_text(&tmp));
    /* /usr/local/bin precedes /usr/bin, so this wins over any leftover wrapper. */
    argv[0] = (char *)"ln"; argv[1] = (char *)"-sf";
    argv[2] = (char *)"/opt/thunderbird/thunderbird";
    argv[3] = (char *)"/usr/local/bin/thunderbird"; argv[4] = NULL;
    (void)osr_run_root(argv);

    /* The tarball ships no .desktop entry (Mozilla leaves that to packagers):
     * without one the app has no menu entry and nothing answers mailto:. */
    (void)osr_write_root("/usr/share/applications/thunderbird.desktop", thunderbird_desktop);
    refresh_desktop_db();
    /* /opt is root-owned, so Thunderbird's own updater cannot apply updates:
     * `osr install thunderbird` (this builder) is the update path. */
    str_free(&tmp); str_free(&tar_path); str_free(&unpacked);
    return 1;
}

/* provide_proteus -- build the theme/wallpaper picker from this repo's Rust
 * crate (../proteus). Proteus is part of the dotfiles: it reads this repo's
 * theme directory and has no meaning apart from it, so a source build is the
 * only route on every target -- same class as wezterm (any.map).
 *
 * Needs a toolchain: list `rust` before `proteus` in the rice (manifest order is
 * the dependency graph, §4). */
static int provide_proteus(void) {
    Str cargo, src, manifest, root;
    char *argv[9];
    int rc;

    str_init(&cargo);
    str_addz(&cargo, osr_mod_home());
    str_addz(&cargo, "/.cargo/bin/cargo");
    argv[0] = (char *)"test"; argv[1] = (char *)"-x"; argv[2] = cargo.p; argv[3] = NULL;
    if (osr_run_user(argv) != 0)
        osr_die("cargo not found for proteus - install 'rust' before proteus "
                "(manifest order, section 4)");

    str_init(&src);
    str_addz(&src, osr_mod_dotfiles());
    str_addz(&src, "/proteus");
    str_init(&manifest);
    str_addz(&manifest, str_text(&src));
    str_addz(&manifest, "/Cargo.toml");
    if (!file_exists(str_text(&manifest)))
        osr_die("proteus sources not found at %s", str_text(&src));

    osr_infof("building proteus from %s", str_text(&src));
    /* --locked: the Cargo.lock this repo was tested against. --root sets the
     * install prefix; the binary lands in $OSR_HOME/.local/bin, which is on
     * PATH for the shell layers. */
    str_init(&root);
    str_addz(&root, osr_mod_home());
    str_addz(&root, "/.local");
    argv[0] = cargo.p; argv[1] = (char *)"install"; argv[2] = (char *)"--locked";
    argv[3] = (char *)"--path"; argv[4] = src.p; argv[5] = (char *)"--root";
    argv[6] = root.p; argv[7] = (char *)"--force"; argv[8] = NULL;
    rc = osr_run_user(argv);
    if (rc != 0) osr_die("proteus build failed (exit %d)", rc);

    str_free(&cargo); str_free(&src); str_free(&manifest); str_free(&root);
    return 1;
}

/* provide_yandex_browser -- Yandex Browser on Debian/Ubuntu from the vendor's
 * own apt repo, the route the vendor documents. A repo and not the one-off .deb:
 * the .deb writes the same source list in its postinst anyway, so adding it up
 * front is the same end state with apt-managed updates from the first run.
 *
 * The key is installed armored and referenced by signed-by=, which needs no gpg
 * on the box (apt reads armored keys by extension) and scopes the key to this
 * one repo instead of trusting it archive-wide the way the deprecated apt-key
 * would. amd64-only: the repo declares i386 but ships an empty index for it. */
static int provide_yandex_browser(void) {
    static const char key_path[] = "/etc/apt/keyrings/yandex-browser.asc";
    Str tmp, list;
    char *argv[9];
    int rc;

    if (strcmp(arch(), "x86_64") != 0)
        osr_die("Yandex publishes no Linux %s browser build (amd64 only)", arch());

    str_init(&tmp);
    if (!make_tmp_file(&tmp)) osr_die("failed to create a temporary file");
    osr_info("adding the Yandex Browser apt repository");
    if (!osr_fetch_download("https://repo.yandex.ru/yandex-browser/YANDEX-BROWSER-KEY.GPG",
                            tmp.p, 0)) {
        (void)unlink(str_text(&tmp));
        osr_die("failed to download the Yandex Browser signing key");
    }
    argv[0] = (char *)"install"; argv[1] = (char *)"-Dm"; argv[2] = (char *)"0644";
    argv[3] = tmp.p; argv[4] = (char *)key_path; argv[5] = NULL;
    (void)osr_run_root(argv);
    (void)unlink(str_text(&tmp));

    str_init(&list);
    str_addz(&list, "deb [arch=amd64 signed-by=");
    str_addz(&list, key_path);
    str_addz(&list, "] https://repo.yandex.ru/yandex-browser/deb stable main\n");
    (void)osr_write_root("/etc/apt/sources.list.d/yandex-browser.list", str_text(&list));

    argv[0] = (char *)"env"; argv[1] = (char *)"DEBIAN_FRONTEND=noninteractive";
    argv[2] = (char *)"apt-get"; argv[3] = (char *)"update"; argv[4] = (char *)"-q";
    argv[5] = NULL;
    if (osr_run_root(argv) != 0)
        osr_warn("apt-get update failed after adding the Yandex Browser repo - "
                 "trying the install anyway");
    argv[0] = (char *)"env"; argv[1] = (char *)"DEBIAN_FRONTEND=noninteractive";
    argv[2] = (char *)"apt-get"; argv[3] = (char *)"install"; argv[4] = (char *)"-y";
    argv[5] = (char *)"yandex-browser-stable"; argv[6] = NULL;
    rc = osr_run_root(argv);
    if (rc != 0) osr_die("failed to install yandex-browser-stable (exit %d)", rc);

    /* _via_source probes `command -v yandex-browser` (§4), but the package
     * installs `yandex-browser-stable` -- hence the symlink, which both
     * satisfies the probe (a rerun is a no-op) and gives the short command. */
    argv[0] = (char *)"ln"; argv[1] = (char *)"-sf";
    argv[2] = (char *)"/usr/bin/yandex-browser-stable";
    argv[3] = (char *)"/usr/local/bin/yandex-browser"; argv[4] = NULL;
    (void)osr_run_root(argv);

    /* The postinst just wrote the vendor's OWN list for this repo, with a
     * different signed-by than ours - which apt 3.0 (Debian 13+) refuses to
     * parse at all, taking every later apt call on the box down with it. Our
     * list was only ever the bootstrap; hand the repo over now. */
    osr_apt_prune_bootstrap_lists();
    argv[0] = (char *)"env"; argv[1] = (char *)"DEBIAN_FRONTEND=noninteractive";
    argv[2] = (char *)"apt-get"; argv[3] = (char *)"update"; argv[4] = (char *)"-q";
    argv[5] = NULL;
    if (osr_run_root(argv) != 0)
        osr_warn("apt-get update failed after handing the Yandex repo to the vendor list");

    str_free(&tmp); str_free(&list);
    return 1;
}

/* provide_betterlockscreen -- Void packages it; no Debian/Ubuntu release does,
 * and it is not a nice-to-have here: modules/i3.sh's config uses
 * `betterlockscreen -l dimblur` as BOTH the xss-lock target (the suspend/lid
 * inhibitor) and the $mod+Escape binding. Absent, the screen never locks and
 * nothing says why.
 *
 * Upstream is one POSIX shell script plus a wrapper, so the install is a copy,
 * not a build -- which is also why there is no version probe. */
static int provide_betterlockscreen(void) {
    /* The runtime closure. i3lock-color maps to plain i3lock on apt (apt.map);
     * feh is already in the rice but is listed because a --module run is not a
     * full rice run. */
    static const char *const deps[] = {
        "i3lock-color", "imagemagick", "xorg-xrandr", "feh", NULL
    };
    Str tag, tmp, tar_path, url, src, unit;
    char *argv[6];

    pkg(deps);
    str_init(&tag);
    tag_of(&tag, "betterlockscreen/betterlockscreen");            /* v4.3.2 */

    str_init(&tmp); str_init(&tar_path); str_init(&url); str_init(&src); str_init(&unit);
    if (!make_tmp_dir(&tmp)) osr_die("failed to create a temporary directory");
    str_addz(&tar_path, str_text(&tmp));
    str_addz(&tar_path, "/bls.tar.gz");
    str_addz(&url, "https://github.com/betterlockscreen/betterlockscreen/archive/refs/tags/");
    str_addz(&url, str_text(&tag));
    str_addz(&url, ".tar.gz");
    if (!osr_fetch_download(str_text(&url), tar_path.p, 0)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to download betterlockscreen %s", str_text(&tag));
    }
    argv[0] = (char *)"tar"; argv[1] = (char *)"-xf"; argv[2] = tar_path.p;
    argv[3] = (char *)"-C"; argv[4] = tmp.p; argv[5] = NULL;
    if (!run_ok(argv)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to extract the betterlockscreen tarball");
    }
    if (!find_file_depth(&src, str_text(&tmp), "betterlockscreen", 2)) {
        rm_rf(str_text(&tmp));
        osr_die("no betterlockscreen script in %s", str_text(&tag));
    }
    argv[0] = (char *)"install"; argv[1] = (char *)"-m"; argv[2] = (char *)"0755";
    argv[3] = src.p; argv[4] = (char *)"/usr/local/bin/betterlockscreen"; argv[5] = NULL;
    if (osr_run_root(argv) != 0) {
        rm_rf(str_text(&tmp));
        osr_die("failed to install betterlockscreen");
    }

    /* The systemd user unit that locks on suspend. Installed only where systemd
     * is the init AND the unit dir exists; on runit/OpenRC the i3 config's
     * `xss-lock --transfer-sleep-lock` carries the inhibitor instead. */
    if (find_file(&unit, str_text(&tmp), "betterlockscreen@.service") &&
        strcmp(osr_mod_init(), "systemd") == 0 &&
        dir_exists("/usr/lib/systemd/system")) {
        argv[0] = (char *)"install"; argv[1] = (char *)"-m"; argv[2] = (char *)"0644";
        argv[3] = unit.p;
        argv[4] = (char *)"/usr/lib/systemd/system/betterlockscreen@.service";
        argv[5] = NULL;
        (void)osr_run_root(argv);
    }
    rm_rf(str_text(&tmp));

    str_free(&tag); str_free(&tmp); str_free(&tar_path);
    str_free(&url); str_free(&src); str_free(&unit);
    if (!osr_have_cmd("betterlockscreen"))
        osr_die("betterlockscreen installed but is not on PATH");
    return 1;
}

/* provide_autotiling -- dwindle-style split direction for i3, the thing that
 * makes a stock i3 stop feeling like it needs `split h` typed before every
 * window. Packaged by Void and by Debian trixie; everywhere else it is PyPI-only,
 * and a `pip install` into the system interpreter is exactly what PEP 668 marks
 * externally-managed. So: take the script and lean on the packaged python3-i3ipc
 * for its one import. */
static int provide_autotiling(void) {
    static const char *const deps[] = { "python3-i3ipc", NULL };
    Str tag, tmp, tar_path, url, src;
    char *argv[6];

    pkg(deps);
    str_init(&tag);
    tag_of(&tag, "nwg-piotr/autotiling");                         /* v1.9.4 */

    str_init(&tmp); str_init(&tar_path); str_init(&url); str_init(&src);
    if (!make_tmp_dir(&tmp)) osr_die("failed to create a temporary directory");
    str_addz(&tar_path, str_text(&tmp));
    str_addz(&tar_path, "/at.tar.gz");
    str_addz(&url, "https://github.com/nwg-piotr/autotiling/archive/refs/tags/");
    str_addz(&url, str_text(&tag));
    str_addz(&url, ".tar.gz");
    if (!osr_fetch_download(str_text(&url), tar_path.p, 0)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to download autotiling %s", str_text(&tag));
    }
    argv[0] = (char *)"tar"; argv[1] = (char *)"-xf"; argv[2] = tar_path.p;
    argv[3] = (char *)"-C"; argv[4] = tmp.p; argv[5] = NULL;
    if (!run_ok(argv)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to extract the autotiling tarball");
    }
    if (!find_path(&src, str_text(&tmp), "/autotiling/main.py")) {
        rm_rf(str_text(&tmp));
        osr_die("no autotiling/main.py in %s", str_text(&tag));
    }
    /* main.py carries its own `#!/usr/bin/env python3` and `if __name__ ==
     * "__main__"` guard, and imports nothing outside the stdlib and i3ipc, so
     * the console_script setuptools would generate adds nothing. */
    argv[0] = (char *)"install"; argv[1] = (char *)"-m"; argv[2] = (char *)"0755";
    argv[3] = src.p; argv[4] = (char *)"/usr/local/bin/autotiling"; argv[5] = NULL;
    if (osr_run_root(argv) != 0) {
        rm_rf(str_text(&tmp));
        osr_die("failed to install autotiling");
    }
    rm_rf(str_text(&tmp));

    str_free(&tag); str_free(&tmp); str_free(&tar_path); str_free(&url); str_free(&src);
    if (!osr_have_cmd("autotiling"))
        osr_die("autotiling installed but is not on PATH");
    return 1;
}

/* --- staged /opt installs ----------------------------------------------------
 * DataGrip and Telegram are the same shape and differ in exactly one thing:
 * who owns the installed tree, and that is not incidental. DataGrip is
 * root-owned, so its own updater cannot patch it and this builder is the update
 * path. Telegram is USER-owned, because it ships an updater that writes into
 * its own directory and a root-owned tree silently breaks it -- for a
 * security-sensitive app, being able to update itself matters more than the
 * tidiness of a root-owned /opt.
 *
 * Both stage inside the destination's parent rather than $TMPDIR: the archives
 * are ~1 GB and unpack to several, which a tmpfs /tmp will not hold, and it
 * makes the swap a same-filesystem rename instead of a cross-device copy. */

/* stage_dir -- `as_root mktemp -d "<parent>/.<what>-XXXXXX"` followed by
 * `as_root chown "$(id -un)" <dir>`: a staging directory on the destination
 * filesystem, handed to the invoking user so the download and unpack run
 * unprivileged like every other builder's. */
static int stage_dir(Str *out, const char *parent, const char *what) {
    Str tpl, who;
    char *argv[5];

    str_init(&tpl);
    str_addz(&tpl, parent);
    str_addz(&tpl, "/.");
    str_addz(&tpl, what);
    str_addz(&tpl, "-XXXXXX");
    argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p"; argv[2] = (char *)parent; argv[3] = NULL;
    (void)osr_run_root(argv);

    str_reset(out);
    argv[0] = (char *)"mktemp"; argv[1] = (char *)"-d"; argv[2] = tpl.p; argv[3] = NULL;
    {
        char **v;
        Str raw;
        str_init(&raw);
        /* as_root, and its stdout is the path -- the one place a builder needs
         * a privileged command's OUTPUT rather than its status. */
        v = (char **)malloc(5 * sizeof(char *));
        if (v == NULL) osr_die_oom();
        if (getuid() != 0) {
            v[0] = (char *)"sudo"; v[1] = argv[0]; v[2] = argv[1]; v[3] = argv[2]; v[4] = NULL;
        } else {
            v[0] = argv[0]; v[1] = argv[1]; v[2] = argv[2]; v[3] = NULL;
        }
        if (osr_run_capture(v, &raw)) {
            str_trim_trailing(&raw, '\n');
            str_addz(out, str_text(&raw));
        }
        free(v);
        str_free(&raw);
    }
    str_free(&tpl);
    if (out->len == 0) return 0;

    str_init(&who);
    {
        char *id[3];
        Str raw;
        str_init(&raw);
        id[0] = (char *)"id"; id[1] = (char *)"-un"; id[2] = NULL;
        if (osr_run_capture(id, &raw)) {
            str_trim_trailing(&raw, '\n');
            str_addz(&who, str_text(&raw));
        }
        str_free(&raw);
    }
    argv[0] = (char *)"chown"; argv[1] = who.p; argv[2] = out->p; argv[3] = NULL;
    (void)osr_run_root(argv);
    str_free(&who);
    return 1;
}

/* rm_rf_root -- `as_root rm -rf <path>`, for a staging dir under /opt whose
 * ownership went back to root, or a tree being replaced. */
static void rm_rf_root(const char *path) {
    char *argv[4];
    argv[0] = (char *)"rm"; argv[1] = (char *)"-rf"; argv[2] = (char *)path; argv[3] = NULL;
    (void)osr_run_root(argv);
}

/* --- DataGrip (JetBrains tarball) --------------------------------------------
 * JetBrains ships DataGrip as a Linux tarball only -- no repo, no deb/rpm. It is
 * a self-contained tree with its own JBR, so unpacking it under /opt IS the
 * supported install; Toolbox does the same thing in $HOME.
 *
 * OSR_DATAGRIP_PREFIX is the one tree this builder owns; anything else on the
 * box (Toolbox, snap, flatpak, a distro package, a hand-unpacked /opt/datagrip-*)
 * is reported and left alone -- §5, we own only what we wrote. */
#define DATAGRIP_PREFIX "/opt/datagrip"

/* datagrip_prefix -- the tree this builder owns, overridable so a test can
 * point the whole install at a sandbox.
 *
 * provide_telegram has had this since it was written; the DataGrip port lost
 * it, and with it the only way to exercise this builder without writing to a
 * real /opt. The paths below therefore compose it at runtime rather than
 * pasting the macro into a string literal. */
static const char *datagrip_prefix(void) {
    return env_str("OSR_DATAGRIP_PREFIX", DATAGRIP_PREFIX);
}

/* dg_path -- <prefix><suffix>, for the handful of paths under the tree. */
static const char *dg_path(const char *suffix) {
    static Str held;
    static int ready = 0;
    if (!ready) { str_init(&held); ready = 1; }
    str_reset(&held);
    str_addz(&held, datagrip_prefix());
    str_addz(&held, suffix);
    return str_text(&held);
}
#define DATAGRIP_FEED \
    "https://data.services.jetbrains.com/products/releases?code=DG&latest=true&type=release"

/* datagrip_latest -- version, url and size of this arch's Linux tarball,
 * resolved from JetBrains' releases feed so no version is hard-coded (G4).
 *
 * The feed is one JSON object per product with a downloads map; the sh original
 * split on commas so the arch key would match EXACTLY ("linux" never matching
 * "linuxARM64"), which here is the same thing as requiring the key to be
 * followed by `":{"link":"`. The size is what turns a ~1 GB fetch into a
 * progress readout; it is optional, so a feed that ever drops it just means a
 * silent download. */
static void datagrip_latest(Str *ver, Str *url, long *size) {
    const char *key;
    Str feed, pat;
    const char *p;

    if (strcmp(arch(), "x86_64") == 0)       key = "linux";
    else if (strcmp(arch(), "aarch64") == 0) key = "linuxARM64";
    else { osr_die("JetBrains publishes no Linux %s DataGrip build (x86_64/aarch64 only)", arch()); return; }

    str_init(&feed);
    if (!osr_fetch_buffer(&feed, DATAGRIP_FEED))
        osr_die("failed to query the JetBrains releases feed for DataGrip");

    str_reset(ver);
    (void)osr_json_string_field(ver, str_text(&feed), "version");

    str_init(&pat);
    str_addc(&pat, '"');
    str_addz(&pat, key);
    str_addz(&pat, "\":{\"link\":\"");
    str_reset(url);
    *size = 0;
    p = strstr(str_text(&feed), str_text(&pat));
    if (p != NULL) {
        p += pat.len;
        while (*p != '\0' && *p != '"') str_addc(url, *p++);
        if (strncmp(p, "\",\"size\":", 9) == 0) {
            p += 9;
            *size = atol(p);
        }
    }
    str_free(&pat);
    str_free(&feed);

    if (strncmp(str_text(url), "https://", 8) != 0 || !ends_with(str_text(url), ".tar.gz"))
        osr_die("could not resolve the DataGrip %s tarball from the JetBrains feed", key);
    if (ver->len == 0)
        osr_die("could not resolve the DataGrip version from the JetBrains feed");
}

/* datagrip_version_at -- the version of an unpacked DataGrip tree.
 * product-info.json sits at the root of every JetBrains IDE tarball. */
static int datagrip_version_at(Str *out, const char *dir) {
    Str path;
    char *text;
    size_t len;
    int ok = 0;

    str_init(&path);
    str_addz(&path, dir);
    str_addz(&path, "/product-info.json");
    text = slurp(str_text(&path), &len);
    str_free(&path);
    if (text == NULL) return 0;
    str_reset(out);
    ok = osr_json_string_field(out, text, "version") && out->len > 0;
    free(text);
    return ok;
}

/* warn_glob -- warn once per directory matching <dir>/<prefix>*, which is the
 * `for f in .../[Dd]ata[Gg]rip*` loops. The case-insensitive character classes
 * the sh globs spelled out are just a case-insensitive prefix compare here. */
static void warn_glob(const char *dir, const char *prefix, const char *skip,
                      const char *fmt) {
    DIR *d = opendir(dir);
    struct dirent *e;
    size_t n = strlen(prefix);

    if (d == NULL) return;
    while ((e = readdir(d)) != NULL) {
        Str path;
        struct stat st;
        size_t i;
        int hit = 1;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        for (i = 0; i < n && hit; i++) {
            char a = e->d_name[i], b = prefix[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) hit = 0;
        }
        if (!hit) continue;
        str_init(&path);
        str_addz(&path, dir);
        str_addc(&path, '/');
        str_addz(&path, e->d_name);
        if (stat(str_text(&path), &st) == 0 && S_ISDIR(st.st_mode) &&
            (skip == NULL || strcmp(str_text(&path), skip) != 0)) {
            osr_warnf(fmt, str_text(&path));
        }
        str_free(&path);
    }
    closedir(d);
}

/* have_pkg_ui -- `command -v <tool> && <tool> <args...>` quietly: the shape both
 * "is there a snap/flatpak of this" probes use. */
static int have_pkg_ui(const char *tool, const char *a1, const char *a2) {
    char *argv[4];
    if (!osr_have_cmd(tool)) return 0;
    argv[0] = (char *)tool; argv[1] = (char *)a1; argv[2] = (char *)a2; argv[3] = NULL;
    return osr_run_quiet(argv) == 0;
}

/* datagrip_report_foreign -- name the DataGrip copies this builder does NOT own.
 * They matter even though nothing here touches them: a Toolbox or snap launcher
 * earlier on PATH is the one that actually opens when the user types `datagrip`,
 * so a silent "upgraded" here would be an upgrade of a tree nobody runs.
 * Reported, never removed (§5, G2). */
static void datagrip_report_foreign(void) {
    Str toolbox;

    if (have_pkg_ui("snap", "list", "datagrip"))
        osr_warn("a DataGrip snap is installed - it is separate from " DATAGRIP_PREFIX
                 " and is not upgraded here");
    if (have_pkg_ui("flatpak", "info", "com.jetbrains.DataGrip"))
        osr_warn("the com.jetbrains.DataGrip flatpak is installed - separate from "
                 DATAGRIP_PREFIX ", not upgraded here");
    if (osr_pkg_native_installed("datagrip"))
        osr_warn("a native 'datagrip' package is installed - separate from "
                 DATAGRIP_PREFIX ", not upgraded here");

    str_init(&toolbox);
    str_addz(&toolbox, osr_mod_home());
    str_addz(&toolbox, "/.local/share/JetBrains/Toolbox/apps");
    warn_glob(str_text(&toolbox), "datagrip", NULL,
              "JetBrains Toolbox has its own DataGrip at %s - Toolbox updates that one, "
              "this module updates " DATAGRIP_PREFIX);
    str_free(&toolbox);
    warn_glob("/opt", "datagrip", datagrip_prefix(),
              "an unpacked DataGrip tree at %s is not owned by this module - "
              "remove it if it is a leftover");
}

/* datagrip_desktop_entry -- launcher symlink + menu entry for the installed
 * tree. The tarball ships no .desktop (JetBrains leaves that to Toolbox), so
 * write the minimal one; the icon comes out of the tree itself, which is why
 * this runs after the tree is in place. Rerun-safe, so it also repairs a box
 * whose entry was lost while the tree stayed current. */
static void datagrip_desktop_entry(void) {
    Str exe, icon, entry;
    char *argv[5];

    str_init(&exe);
    str_addz(&exe, dg_path("/bin/datagrip"));
    if (access(str_text(&exe), X_OK) != 0) {
        str_reset(&exe);
        str_addz(&exe, dg_path("/bin/datagrip.sh"));
    }
    if (access(str_text(&exe), X_OK) != 0)
        osr_die("no DataGrip launcher under %s/bin", datagrip_prefix());

    /* /usr/local/bin precedes /usr/bin, and this is also the via_source probe. */
    argv[0] = (char *)"ln"; argv[1] = (char *)"-sf"; argv[2] = exe.p;
    argv[3] = (char *)"/usr/local/bin/datagrip"; argv[4] = NULL;
    (void)osr_run_root(argv);

    str_init(&icon);
    if (file_exists(dg_path("/bin/datagrip.png")))      str_addz(&icon, dg_path("/bin/datagrip.png"));
    else if (file_exists(dg_path("/bin/datagrip.svg"))) str_addz(&icon, dg_path("/bin/datagrip.svg"));
    else osr_warn("no datagrip icon in the tarball - the menu entry will use the theme fallback");

    /* StartupWMClass is what the IDE actually sets on its window; without it the
     * taskbar shows a second, unnamed entry. */
    str_init(&entry);
    str_addz(&entry, "[Desktop Entry]\nName=DataGrip\nComment=Database IDE\nExec=");
    str_addz(&entry, str_text(&exe));
    str_addz(&entry, " %f\nIcon=");
    str_addz(&entry, icon.len > 0 ? str_text(&icon) : "datagrip");
    str_addz(&entry, "\nTerminal=false\nType=Application\n"
                     "Categories=Development;IDE;Database;\n"
                     "Keywords=sql;database;jetbrains;\n"
                     "StartupNotify=true\nStartupWMClass=jetbrains-datagrip\n");
    (void)osr_write_root("/usr/share/applications/datagrip.desktop", str_text(&entry));
    refresh_desktop_db();

    str_free(&exe); str_free(&icon); str_free(&entry);
}

/* provide_datagrip -- install or UPGRADE DataGrip from the vendor tarball.
 *
 * Idempotency goes beyond the caller's `command -v datagrip` probe (§2, same
 * shape as provide_chafa): presence is not sufficiency for an IDE that ships a
 * new build every few weeks, so the builder compares the installed tree's
 * product-info.json against the feed and returns early only when they match.
 * That is what makes `osr module datagrip` the update path. */
static int provide_datagrip(void) {
    static const char *const deps[] = { "tar", "gzip", NULL };
    Str ver, url, have, tmp, tar_path, src, parent;
    long size = 0;
    char *argv[6];

    pkg(deps);
    str_init(&ver); str_init(&url);
    datagrip_latest(&ver, &url, &size);
    datagrip_report_foreign();

    str_init(&have);
    if (datagrip_version_at(&have, datagrip_prefix()) &&
        strcmp(str_text(&have), str_text(&ver)) == 0) {
        osr_infof("DataGrip %s is already the current release - skipping the download",
                  str_text(&ver));
        datagrip_desktop_entry();
        str_free(&ver); str_free(&url); str_free(&have);
        return 1;
    }
    if (have.len > 0) osr_infof("upgrading DataGrip %s -> %s", str_text(&have), str_text(&ver));
    else              osr_infof("installing DataGrip %s", str_text(&ver));
    str_free(&have);

    /* Staged beside the tree it will become, so the `mv` into place is a
     * rename within one filesystem rather than a copy of a gigabyte -- and so
     * that a test pointing OSR_DATAGRIP_PREFIX at a sandbox does not have the
     * staging directory land in the real /opt anyway. provide_telegram derives
     * its parent the same way. */
    str_init(&parent);
    {
        const char *pfx = datagrip_prefix();
        const char *slash = strrchr(pfx, '/');
        if (slash == NULL || slash == pfx) str_addz(&parent, "/");
        else str_add(&parent, pfx, (size_t)(slash - pfx));
    }
    str_init(&tmp);
    if (!stage_dir(&tmp, str_text(&parent), "datagrip"))
        osr_die("failed to create a staging directory under %s", str_text(&parent));
    {
        Str base;
        str_init(&base);
        base_of(&base, str_text(&url));
        osr_infof("downloading %s (%ld MiB)", str_text(&base), size / 1048576);
        str_free(&base);
    }
    str_init(&tar_path);
    str_addz(&tar_path, str_text(&tmp));
    str_addz(&tar_path, "/datagrip.tar.gz");
    if (!osr_fetch_download(str_text(&url), tar_path.p, size)) {
        rm_rf_root(str_text(&tmp));
        osr_die("failed to download %s", str_text(&url));
    }
    argv[0] = (char *)"tar"; argv[1] = (char *)"-xzf"; argv[2] = tar_path.p;
    argv[3] = (char *)"-C"; argv[4] = tmp.p; argv[5] = NULL;
    if (!run_ok(argv)) {
        rm_rf_root(str_text(&tmp));
        osr_die("failed to extract the DataGrip tarball");
    }
    (void)unlink(str_text(&tar_path));

    /* The tarball unpacks into a single versioned dir (DataGrip-2026.2.3/). */
    str_init(&src);
    if (!first_subdir(&src, str_text(&tmp))) {
        rm_rf_root(str_text(&tmp));
        osr_die("the DataGrip tarball has an unexpected layout (no product-info.json)");
    }
    {
        Str info;
        int ok;
        str_init(&info);
        str_addz(&info, str_text(&src));
        str_addz(&info, "/product-info.json");
        ok = file_exists(str_text(&info));
        str_free(&info);
        if (!ok) {
            rm_rf_root(str_text(&tmp));
            osr_die("the DataGrip tarball has an unexpected layout (no product-info.json)");
        }
    }
    rm_rf_root(datagrip_prefix());
    argv[0] = (char *)"mv"; argv[1] = src.p; argv[2] = (char *)datagrip_prefix();
    argv[3] = NULL;
    if (osr_run_root(argv) != 0) {
        rm_rf_root(str_text(&tmp));
        osr_die("failed to install DataGrip into %s", datagrip_prefix());
    }
    rm_rf_root(str_text(&tmp));
    argv[0] = (char *)"chown"; argv[1] = (char *)"-R"; argv[2] = (char *)"0:0";
    argv[3] = (char *)datagrip_prefix(); argv[4] = NULL;
    (void)osr_run_root(argv);
    datagrip_desktop_entry();
    /* /opt is root-owned, so the IDE's own updater cannot patch this tree:
     * `osr module datagrip` (this builder) is the update path. */
    str_free(&ver); str_free(&url); str_free(&tmp); str_free(&tar_path);
    str_free(&src); str_free(&parent);
    return 1;
}

/* --- Telegram Desktop (vendor tarball) ---------------------------------------
 * telegram.org/desktop's Linux button is a redirect to the current
 * tsetup.<version>.tar.xz, which unpacks to a self-contained Telegram/ tree.
 * Distro packages exist, but they lag -- a messenger whose protocol moves is one
 * of the few things worth taking straight from upstream.
 *
 * Leaving the updater enabled also means Telegram writes its OWN .desktop entry
 * and icon into ~/.local/share on first run, so unlike DataGrip there is no menu
 * entry to write here. */
#define TELEGRAM_PREFIX "/opt/telegram-desktop"

/* telegram_latest -- version, url and size of the current release, resolved
 * from the redirect the download link answers with (G4: no version is ever
 * written down here). */
static void telegram_latest(Str *ver, Str *url, long *size) {
    Str file;
    const char *p;

    if (strcmp(arch(), "x86_64") != 0)
        osr_die("Telegram publishes no Linux %s desktop tarball (x86_64 only)", arch());
    str_reset(url);
    if (!osr_fetch_final_url(url, env_str("OSR_TELEGRAM_URL",
                                          "https://telegram.org/dl/desktop/linux")) ||
        !ends_with(str_text(url), ".tar.xz") ||
        strstr(str_text(url), "tsetup.") == NULL)
        osr_die("could not resolve the Telegram tarball from %s",
                env_str("OSR_TELEGRAM_URL", "https://telegram.org/dl/desktop/linux"));

    str_init(&file);
    base_of(&file, str_text(url));                     /* tsetup.5.9.0.tar.xz */
    p = str_text(&file);
    if (strncmp(p, "tsetup.", 7) == 0) p += 7;
    str_reset(ver);
    str_add(ver, p, strlen(p) - 7);                    /* drop ".tar.xz" */
    str_free(&file);
    *size = osr_fetch_remote_size(str_text(url));
    if (*size < 0) *size = 0;
}

/* telegram_report_foreign -- name the copies of Telegram this builder does not
 * own. Reported, never removed (§5, G2): a snap/flatpak/distro launcher earlier
 * on PATH is the one that opens, and each keeps its own tdata, so the wrong one
 * looks like "my chats are gone". */
static void telegram_report_foreign(void) {
    if (have_pkg_ui("snap", "list", "telegram-desktop"))
        osr_warn("a telegram-desktop snap is installed - separate from " TELEGRAM_PREFIX
                 ", not updated here");
    if (have_pkg_ui("flatpak", "info", "org.telegram.desktop"))
        osr_warn("the org.telegram.desktop flatpak is installed - separate from "
                 TELEGRAM_PREFIX ", not updated here");
    if (osr_pkg_native_installed("telegram-desktop"))
        osr_warn("a native 'telegram-desktop' package is installed - separate from "
                 TELEGRAM_PREFIX ", not updated here");
}

/* provide_telegram -- install or update Telegram Desktop from the vendor
 * tarball.
 *
 * Version-idempotent, like provide_datagrip, but the installed version is a
 * STAMP FILE this builder writes: the binary has no -version flag and the tree
 * carries no manifest. When Telegram has updated itself the stamp goes stale and
 * a rerun reinstalls the same version once -- a wasted 80 MB in the rare case,
 * against re-downloading on every rice run if presence were the test. */
static int provide_telegram(void) {
    static const char *const deps[] = { "tar", "xz", NULL };
    const char *prefix = env_str("OSR_TELEGRAM_PREFIX", TELEGRAM_PREFIX);
    Str ver, url, tmp, tar_path, src, stamp, binary, parent;
    long size = 0;
    char *argv[6];

    pkg(deps);
    str_init(&ver); str_init(&url);
    telegram_latest(&ver, &url, &size);
    telegram_report_foreign();

    str_init(&stamp); str_init(&binary);
    str_addz(&stamp, prefix); str_addz(&stamp, "/.osr-version");
    str_addz(&binary, prefix); str_addz(&binary, "/Telegram");
    if (access(str_text(&binary), X_OK) == 0 && file_exists(str_text(&stamp))) {
        char *text;
        size_t len;
        int same = 0;
        text = slurp(str_text(&stamp), &len);
        if (text != NULL) {
            Str got;
            str_init(&got);
            str_add(&got, text, len);
            str_trim_trailing(&got, '\n');
            same = strcmp(str_text(&got), str_text(&ver)) == 0;
            str_free(&got);
            free(text);
        }
        if (same) {
            osr_infof("Telegram Desktop %s is already installed - skipping the download",
                      str_text(&ver));
            argv[0] = (char *)"ln"; argv[1] = (char *)"-sf"; argv[2] = binary.p;
            argv[3] = (char *)"/usr/local/bin/telegram-desktop"; argv[4] = NULL;
            (void)osr_run_root(argv);
            str_free(&ver); str_free(&url); str_free(&stamp); str_free(&binary);
            return 1;
        }
    }

    str_init(&parent);
    {
        const char *slash = strrchr(prefix, '/');
        if (slash == NULL || slash == prefix) str_addz(&parent, "/");
        else str_add(&parent, prefix, (size_t)(slash - prefix));
    }
    str_init(&tmp);
    if (!stage_dir(&tmp, str_text(&parent), "telegram"))
        osr_die("failed to create a staging directory under %s", str_text(&parent));
    osr_infof("installing Telegram Desktop %s", str_text(&ver));
    str_init(&tar_path);
    str_addz(&tar_path, str_text(&tmp));
    str_addz(&tar_path, "/telegram.tar.xz");
    if (!osr_fetch_download(str_text(&url), tar_path.p, size)) {
        rm_rf_root(str_text(&tmp));
        osr_die("failed to download %s", str_text(&url));
    }
    argv[0] = (char *)"tar"; argv[1] = (char *)"-xf"; argv[2] = tar_path.p;
    argv[3] = (char *)"-C"; argv[4] = tmp.p; argv[5] = NULL;
    if (!run_ok(argv)) {
        rm_rf_root(str_text(&tmp));
        osr_die("failed to extract the Telegram tarball");
    }
    (void)unlink(str_text(&tar_path));

    str_init(&src);
    if (!first_subdir(&src, str_text(&tmp))) {
        rm_rf_root(str_text(&tmp));
        osr_die("the Telegram tarball has an unexpected layout (no Telegram binary)");
    }
    {
        Str exe, mark;
        int ok;
        str_init(&exe);
        str_addz(&exe, str_text(&src));
        str_addz(&exe, "/Telegram");
        ok = access(str_text(&exe), X_OK) == 0;
        str_free(&exe);
        if (!ok) {
            rm_rf_root(str_text(&tmp));
            osr_die("the Telegram tarball has an unexpected layout (no Telegram binary)");
        }
        str_init(&mark);
        str_addz(&mark, str_text(&src));
        str_addz(&mark, "/.osr-version");
        {
            FILE *f = fopen(str_text(&mark), "w");
            if (f != NULL) { fprintf(f, "%s\n", str_text(&ver)); fclose(f); }
        }
        str_free(&mark);
    }
    rm_rf_root(prefix);
    argv[0] = (char *)"mv"; argv[1] = src.p; argv[2] = (char *)prefix; argv[3] = NULL;
    if (osr_run_root(argv) != 0) {
        rm_rf_root(str_text(&tmp));
        osr_die("failed to install Telegram into %s", prefix);
    }
    rm_rf_root(str_text(&tmp));
    /* The riced user owns the tree so Telegram's own updater can replace the
     * binary - that account is the one that runs it (§8). */
    argv[0] = (char *)"chown"; argv[1] = (char *)"-R";
    argv[2] = (char *)osr_mod_user(); argv[3] = (char *)prefix; argv[4] = NULL;
    (void)osr_run_root(argv);
    argv[0] = (char *)"ln"; argv[1] = (char *)"-sf"; argv[2] = binary.p;
    argv[3] = (char *)"/usr/local/bin/telegram-desktop"; argv[4] = NULL;
    (void)osr_run_root(argv);

    str_free(&ver); str_free(&url); str_free(&tmp); str_free(&tar_path);
    str_free(&src); str_free(&stamp); str_free(&binary); str_free(&parent);
    return 1;
}

/* provide_yandex_browser_deb -- Yandex Browser on a target with no package of
 * its own (Void). The vendor publishes deb + rpm and nothing else, and there is
 * no void-packages template, so the only route is unpacking the official .deb:
 * a self-contained Chromium tree under /opt/yandex plus a launcher symlink and
 * .desktop entries, with no maintainer scripts that matter outside dpkg. bsdtar
 * reads both the outer `ar` container and the inner compressed data tarball, so
 * no dpkg/binutils is needed.
 *
 * ./etc is deliberately left behind: everything the deb puts there exists for
 * dpkg's world only -- a daily cron job that runs `apt-get update`, and an
 * autostart entry whose whole job is the "make me your default browser" nag.
 *
 * The one thing dpkg's postinst did that has to be repeated: the SUID sandbox
 * helper gets its setuid bit back. bsdtar drops it when not extracting as root,
 * and Chromium refuses to start without either that or unprivileged user
 * namespaces. */

/* yb_deb_url -- the current yandex-browser-stable .deb URL, resolved from the
 * vendor repo's own Packages index (uncompressed, ~4 KB, one package per
 * stanza) so no version is ever hard-coded here (G4). */
static void yb_deb_url(Str *out) {
    static const char base[] = "https://repo.yandex.ru/yandex-browser/deb";
    Str index;
    size_t pos = 0;
    Line l;
    int in_stanza = 0, found = 0;

    str_init(&index);
    {
        Str feed;
        str_init(&feed);
        str_addz(&feed, base);
        str_addz(&feed, "/dists/stable/main/binary-amd64/Packages");
        (void)osr_fetch_buffer(&index, str_text(&feed));
        str_free(&feed);
    }
    str_reset(out);
    while (!found && next_line(str_text(&index), index.len, &pos, &l)) {
        Str line;
        str_init(&line);
        str_add(&line, l.start, l.len);
        if (strcmp(str_text(&line), "Package: yandex-browser-stable") == 0) {
            in_stanza = 1;
        } else if (in_stanza && strncmp(str_text(&line), "Filename:", 9) == 0) {
            const char *p = str_text(&line) + 9;
            while (is_space(*p)) p++;
            str_addz(out, base);
            str_addc(out, '/');
            while (*p != '\0' && !is_space(*p)) str_addc(out, *p++);
            found = 1;
        }
        str_free(&line);
    }
    str_free(&index);
    if (!found)
        osr_die("could not resolve the yandex-browser-stable .deb from the vendor index");
}

static int provide_yandex_browser_deb(void) {
    /* The deb declares its shared libraries as dependencies; unpacked, nothing
     * resolves them - the map row lists the same closure under one logical name. */
    static const char *const deps[] = { "yandex-browser-deps", NULL };
    Str url, tmp, deb, data, root, browser;
    char *argv[6];

    if (strcmp(arch(), "x86_64") != 0)
        osr_die("Yandex publishes no Linux %s browser build (amd64 only)", arch());
    pkg(deps);
    if (!osr_have_cmd("bsdtar"))
        osr_die("bsdtar (libarchive) is required to unpack the Yandex Browser .deb");

    str_init(&url);
    yb_deb_url(&url);
    str_init(&tmp); str_init(&deb); str_init(&data); str_init(&root); str_init(&browser);
    if (!make_tmp_dir(&tmp)) osr_die("failed to create a temporary directory");
    {
        Str base;
        str_init(&base);
        base_of(&base, str_text(&url));
        osr_infof("downloading Yandex Browser (%s)", str_text(&base));
        str_free(&base);
    }
    str_addz(&deb, str_text(&tmp));
    str_addz(&deb, "/yb.deb");
    if (!osr_fetch_download(str_text(&url), deb.p, 0)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to download %s", str_text(&url));
    }
    argv[0] = (char *)"bsdtar"; argv[1] = (char *)"-xf"; argv[2] = deb.p;
    argv[3] = (char *)"-C"; argv[4] = tmp.p; argv[5] = NULL;
    if (!run_ok(argv)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to open the Yandex Browser .deb");
    }
    /* `find <tmp> -maxdepth 1 -name 'data.tar*'`: the compression varies. */
    {
        DIR *d = opendir(str_text(&tmp));
        struct dirent *e;
        if (d != NULL) {
            while (data.len == 0 && (e = readdir(d)) != NULL) {
                if (strncmp(e->d_name, "data.tar", 8) != 0) continue;
                str_addz(&data, str_text(&tmp));
                str_addc(&data, '/');
                str_addz(&data, e->d_name);
            }
            closedir(d);
        }
    }
    if (data.len == 0) {
        rm_rf(str_text(&tmp));
        osr_die("no data.tar in the Yandex Browser .deb");
    }
    str_addz(&root, str_text(&tmp));
    str_addz(&root, "/root");
    argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p"; argv[2] = root.p; argv[3] = NULL;
    (void)osr_run(argv);
    argv[0] = (char *)"bsdtar"; argv[1] = (char *)"-xf"; argv[2] = data.p;
    argv[3] = (char *)"-C"; argv[4] = root.p; argv[5] = NULL;
    if (!run_ok(argv)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to unpack the Yandex Browser .deb");
    }
    str_addz(&browser, str_text(&root));
    str_addz(&browser, "/opt/yandex/browser/yandex_browser");
    if (access(str_text(&browser), X_OK) != 0) {
        rm_rf(str_text(&tmp));
        osr_die("no /opt/yandex/browser/yandex_browser in the .deb - its layout changed");
    }

    argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
    argv[2] = (char *)"/opt/yandex"; argv[3] = NULL;
    (void)osr_run_root(argv);
    rm_rf_root("/opt/yandex/browser");
    str_reset(&browser);
    str_addz(&browser, str_text(&root));
    str_addz(&browser, "/opt/yandex/browser");
    argv[0] = (char *)"cp"; argv[1] = (char *)"-a"; argv[2] = browser.p;
    argv[3] = (char *)"/opt/yandex/"; argv[4] = NULL;
    if (osr_run_root(argv) != 0) {
        rm_rf(str_text(&tmp));
        osr_die("failed to install Yandex Browser into /opt");
    }
    /* ./usr is the launcher symlink, the two .desktop entries, icons and appdata. */
    str_reset(&browser);
    str_addz(&browser, str_text(&root));
    str_addz(&browser, "/usr/.");
    argv[0] = (char *)"cp"; argv[1] = (char *)"-a"; argv[2] = browser.p;
    argv[3] = (char *)"/usr/"; argv[4] = NULL;
    (void)osr_run_root(argv);
    rm_rf(str_text(&tmp));

    argv[0] = (char *)"chmod"; argv[1] = (char *)"4755";
    argv[2] = (char *)"/opt/yandex/browser/yandex_browser-sandbox"; argv[3] = NULL;
    (void)osr_run_root(argv);
    argv[0] = (char *)"ln"; argv[1] = (char *)"-sf";
    argv[2] = (char *)"/opt/yandex/browser/yandex-browser";
    argv[3] = (char *)"/usr/local/bin/yandex-browser"; argv[4] = NULL;
    (void)osr_run_root(argv);
    refresh_desktop_db();

    str_free(&url); str_free(&tmp); str_free(&deb);
    str_free(&data); str_free(&root); str_free(&browser);
    return 1;
}

/* --- AmneziaVPN --------------------------------------------------------------
 * The release binary if there is one for this arch, and a full Qt/QML source
 * build if there is not. The fallback lives inside the builder rather than
 * across two pkgmap rows: the row still resolves to exactly one method. */

static int provide_amneziavpn_source(void);

static int provide_amneziavpn(void) {
    Str tag, tmp, tar_path, url, bin;
    char *argv[10];
    int rc;

    if (strcmp(arch(), "x86_64") != 0) {
        osr_warnf("no AmneziaVPN release binary for arch %s - falling back to the source build",
                  arch());
        return provide_amneziavpn_source();
    }
    /* Quiet: an unreachable API is a reason to fall back, not to end the run. */
    str_init(&tag);
    if (osr_github_latest_quiet(&tag, "amnezia-vpn/amnezia-client")) {   /* 4.8.21.0, no v */
        str_init(&tmp); str_init(&tar_path); str_init(&url); str_init(&bin);
        if (!make_tmp_dir(&tmp)) osr_die("failed to create a temporary directory");
        str_addz(&tar_path, str_text(&tmp));
        str_addz(&tar_path, "/amnezia.tar");
        str_addz(&url, "https://github.com/amnezia-vpn/amnezia-client/releases/download/");
        str_addz(&url, str_text(&tag));
        str_addz(&url, "/AmneziaVPN_");
        str_addz(&url, str_text(&tag));
        str_addz(&url, "_linux_x64.tar");
        if (osr_fetch_download(str_text(&url), tar_path.p, 0)) {
            argv[0] = (char *)"tar"; argv[1] = (char *)"-xf"; argv[2] = tar_path.p;
            argv[3] = (char *)"-C"; argv[4] = tmp.p; argv[5] = NULL;
            if (run_ok(argv)) (void)find_path(&bin, str_text(&tmp), ".bin");
        }
        if (bin.len > 0) {
            (void)chmod(str_text(&bin), 0755);
            argv[0] = bin.p; argv[1] = (char *)"install"; argv[2] = (char *)"--root";
            argv[3] = (char *)"/opt/AmneziaVPN"; argv[4] = (char *)"--accept-licenses";
            argv[5] = (char *)"--accept-messages"; argv[6] = (char *)"--confirm-command";
            argv[7] = (char *)"-p"; argv[8] = (char *)"minimal"; argv[9] = NULL;
            rc = osr_run_root(argv);
            rm_rf(str_text(&tmp));
            /* A release that downloaded but refused to install is a broken
             * target, not a "no binary available" case - a 40-minute build will
             * not fix it. */
            if (rc != 0) osr_die("AmneziaVPN headless install failed (exit %d)", rc);
            argv[0] = (char *)"ln"; argv[1] = (char *)"-sf";
            argv[2] = (char *)"/opt/AmneziaVPN/AmneziaVPN";
            argv[3] = (char *)"/usr/local/bin/amneziavpn"; argv[4] = NULL;
            (void)osr_run_root(argv);
            str_free(&tag); str_free(&tmp); str_free(&tar_path);
            str_free(&url); str_free(&bin);
            return 1;
        }
        rm_rf(str_text(&tmp));
        str_free(&tmp); str_free(&tar_path); str_free(&url); str_free(&bin);
    }
    osr_warnf("AmneziaVPN release binary unavailable (tag='%s') - falling back to the "
              "source build. Have God with you: a full Qt/QML compile and link, plus its "
              "conan deps, can be >24GB RSS observed. Lower OSR_BUILD_JOBS or add swap "
              "if it OOMs.", tag.len > 0 ? str_text(&tag) : "unresolved");
    str_free(&tag);
    return provide_amneziavpn_source();
}

/* provide_amneziavpn_source -- build the client from source, the route upstream
 * documents. NOT a route of its own: nothing maps to it. It is the last resort
 * inside provide_amneziavpn, reached only when the release binary is unavailable.
 *
 * Upstream's deploy/build.sh only finds Qt in the Qt-online-installer layout
 * (~/Qt, /opt/Qt) and never a distro Qt6, and it is a thin cmake configure+build
 * that never installs anything -- so drive cmake directly instead: same three
 * commands, no env-var gymnastics, and `cmake --install` puts the tree where the
 * shipped systemd unit already expects it.
 *
 * HEAVY, and a real-desktop concern (§9). Linking is the peak -- >24GB RSS
 * observed -- so OSR_BUILD_JOBS caps build parallelism (default 1: one link at
 * a time). */
static int provide_amneziavpn_source(void) {
    /* Qt 6.10+ with the components client/ and service/ ask for. No
     * qt6-webengine: nothing links it now and it is the single largest dep. */
    static const char *const deps[] = {
        "build", "cmake", "git", "conan", "openssl", "qt6-base", "qt6-declarative",
        "qt6-svg", "qt6-tools", "qt6-5compat", "qt6-remoteobjects", "qt6-wayland", NULL
    };
    Str src, bld, cml, jobs;
    char *argv[10];

    /* The caller's §2 probe is `command -v amneziavpn`; this catches the other
     * half - a prebuilt install that left AmneziaVPN on PATH under its own name. */
    if (osr_have_cmd("AmneziaVPN")) {
        osr_info("AmneziaVPN already installed - skipping the source build");
        return 1;
    }
    if (strcmp(arch(), "x86_64") != 0)
        osr_warnf("AmneziaVPN upstream builds/tests x86_64 only - building on %s anyway",
                  arch());
    pkg(deps);

    /* A failed build KEEPS the checkout so a retry resumes instead of
     * recompiling from scratch (same contract as provide_wezterm). */
    str_init(&src);
    str_addz(&src, tmp_root());
    str_addz(&src, "/osr-amneziavpn-src");
    str_init(&cml);
    str_addz(&cml, str_text(&src));
    str_addz(&cml, "/CMakeLists.txt");
    if (file_exists(str_text(&cml))) {
        osr_infof("reusing the existing amnezia-client checkout (%s) - rebuild is incremental",
                  str_text(&src));
        /* Only the reuse path needs this; --recursive already did it on a
         * fresh clone. */
        argv[0] = (char *)"git"; argv[1] = (char *)"submodule"; argv[2] = (char *)"update";
        argv[3] = (char *)"--init"; argv[4] = (char *)"--recursive"; argv[5] = NULL;
        if (run_in_dir(str_text(&src), AS_USER, argv) != 0)
            osr_die("amnezia-client submodule checkout failed");
    } else {
        argv[0] = (char *)"rm"; argv[1] = (char *)"-rf"; argv[2] = src.p; argv[3] = NULL;
        (void)osr_run_user(argv);
        argv[0] = (char *)"git"; argv[1] = (char *)"clone"; argv[2] = (char *)"--depth";
        argv[3] = (char *)"1"; argv[4] = (char *)"--recursive";
        argv[5] = (char *)"https://github.com/amnezia-vpn/amnezia-client.git";
        argv[6] = src.p; argv[7] = NULL;
        if (osr_run_user(argv) != 0) osr_die("failed to clone amnezia-client");
    }

    str_init(&bld);
    str_addz(&bld, str_text(&src));
    str_addz(&bld, "/build");
    /* Configure and build as OSR_USER: cmake's conan provider writes ~/.conan2
     * and pulls the prebuilt recipes, which must NOT land in root's home (§8).
     * CMAKE_PREFIX_PATH=/usr points it at the distro Qt6. */
    argv[0] = (char *)"cmake"; argv[1] = (char *)"-S"; argv[2] = src.p;
    argv[3] = (char *)"-B"; argv[4] = bld.p;
    argv[5] = (char *)"-DCMAKE_BUILD_TYPE=Release";
    argv[6] = (char *)"-DCMAKE_PREFIX_PATH=/usr";
    argv[7] = (char *)"-DCMAKE_INSTALL_PREFIX=/opt/AmneziaVPN";
    argv[8] = NULL;
    if (osr_run_user(argv) != 0) osr_die("amnezia-client cmake configure failed");

    str_init(&jobs);
    str_addz(&jobs, "CMAKE_BUILD_PARALLEL_LEVEL=");
    str_addz(&jobs, env_str("OSR_BUILD_JOBS", "1"));
    argv[0] = (char *)"env"; argv[1] = jobs.p; argv[2] = (char *)"cmake";
    argv[3] = (char *)"--build"; argv[4] = bld.p; argv[5] = NULL;
    if (osr_run_user(argv) != 0)
        osr_die("amnezia-client build failed (checkout kept at %s - rerun to resume; "
                "OOM? lower OSR_BUILD_JOBS or add swap)", str_text(&src));
    argv[0] = (char *)"cmake"; argv[1] = (char *)"--install"; argv[2] = bld.p;
    argv[3] = (char *)"--component"; argv[4] = (char *)"AmneziaVPN"; argv[5] = NULL;
    if (osr_run_root(argv) != 0) osr_die("amnezia-client install failed");

    /* What upstream's deploy/data/linux/post_install.sh does, minus the SteamOS
     * and killall branches: the component drops the unit/desktop/icon at the
     * prefix root, and something has to place them. */
    argv[0] = (char *)"install"; argv[1] = (char *)"-Dm"; argv[2] = (char *)"0644";
    argv[3] = (char *)"/opt/AmneziaVPN/AmneziaVPN.desktop";
    argv[4] = (char *)"/usr/share/applications/AmneziaVPN.desktop"; argv[5] = NULL;
    if (osr_run_root(argv) != 0) osr_warn("failed to install the AmneziaVPN desktop entry");
    argv[3] = (char *)"/opt/AmneziaVPN/AmneziaVPN.png";
    argv[4] = (char *)"/usr/share/pixmaps/AmneziaVPN.png";
    if (osr_run_root(argv) != 0) osr_warn("failed to install the AmneziaVPN icon");

    /* Exec=AmneziaVPN in the .desktop, and the rice autostart probes
     * `AmneziaVPN`; `amneziavpn` matches what provide_amneziavpn puts on PATH.
     * Both, one binary. */
    argv[0] = (char *)"ln"; argv[1] = (char *)"-sf";
    argv[2] = (char *)"/opt/AmneziaVPN/bin/AmneziaVPN";
    argv[3] = (char *)"/usr/local/bin/AmneziaVPN"; argv[4] = NULL;
    (void)osr_run_root(argv);
    argv[3] = (char *)"/usr/local/bin/amneziavpn";
    (void)osr_run_root(argv);

    /* The privileged helper the client talks to. systemd-only unit, so no
     * service enable on other inits - the client is simply unusable there. */
    if (strcmp(osr_mod_init(), "systemd") == 0) {
        argv[0] = (char *)"install"; argv[1] = (char *)"-m"; argv[2] = (char *)"0644";
        argv[3] = (char *)"/opt/AmneziaVPN/AmneziaVPN.service";
        argv[4] = (char *)"/etc/systemd/system/AmneziaVPN.service"; argv[5] = NULL;
        (void)osr_run_root(argv);
        argv[0] = (char *)"systemctl"; argv[1] = (char *)"daemon-reload"; argv[2] = NULL;
        (void)osr_run_root(argv);
        (void)osr_service_enable("AmneziaVPN");
    } else {
        osr_warnf("AmneziaVPN ships a systemd unit only - the background service is not "
                  "enabled on %s", osr_mod_init());
    }
    argv[0] = (char *)"rm"; argv[1] = (char *)"-rf"; argv[2] = src.p; argv[3] = NULL;
    (void)osr_run_user(argv);

    str_free(&src); str_free(&bld); str_free(&cml); str_free(&jobs);
    return 1;
}

/* --- GPaste ------------------------------------------------------------------
 * GPaste is versioned AGAINST GNOME Shell: v50.x is the branch that speaks the
 * Shell 50 extension API, v45.x speaks 45's. "Latest" is therefore the wrong
 * question -- the right tag is the newest one whose major matches the running
 * gnome-shell, which is what gpaste_tag resolves.
 *
 * Source is the only route on Debian/Ubuntu: resolute ships 45.3-5, whose only
 * concession to modern GNOME is a downstream patch widening metadata.json's
 * shell-version list to "50". The JS behind it is still the 45 extension. What
 * that costs, all three from one root: the Shell refuses the extension's
 * GrabAccelerators call, so the daemon logs "falling back to X11 keybinder" and
 * starts polling X selections under Xwayland -- that is the flickering selection
 * and the twitching panel. And with no working extension, a Wayland session
 * gives the daemon no clipboard to watch at all, so the history stays empty.
 * One version mismatch, three symptoms.
 *
 * The prefix is /usr, not the /usr/local every other builder here uses, and that
 * is forced by GIRepository rather than chosen: its typelib search path is the
 * libdir gobject-introspection itself was compiled with plus $GI_TYPELIB_PATH --
 * XDG_DATA_DIRS does not enter into it. A /usr/local prefix therefore parks
 * GPaste-2.typelib somewhere gnome-shell will never look, and the extension dies
 * on `Typelib file ... not found` while every other part of the install looks
 * perfectly healthy. */
#define GPASTE_REPO "Keruspe/GPaste"

/* first_major -- the major version out of a `<tool> --version`-style line, whose
 * shape here is "<words> <version>": lib/build.sh took the LAST word and then
 * everything before its first dot. */
static int first_major(Str *out, const char *text) {
    const char *last = text;
    const char *p;

    for (p = text; *p != '\0'; p++) {
        if (*p == ' ') last = p + 1;
    }
    if (*last == '\0') return 0;
    str_reset(out);
    while (*last != '\0' && *last != '.' && !is_space(*last)) str_addc(out, *last++);
    return out->len > 0;
}

/* tool_major -- the major reported by a tool that may not be installed. */
static int tool_major(Str *out, const char *tool, const char *arg) {
    Str raw;
    char *argv[3];
    size_t pos = 0;
    Line l;
    int ok = 0;

    if (!osr_have_cmd(tool)) return 0;
    str_init(&raw);
    argv[0] = (char *)tool; argv[1] = (char *)arg; argv[2] = NULL;
    if (osr_run_capture(argv, &raw) && next_line(str_text(&raw), raw.len, &pos, &l)) {
        Str line;
        str_init(&line);
        str_add(&line, l.start, l.len);
        ok = first_major(out, str_text(&line));
        str_free(&line);
    }
    str_free(&raw);
    return ok;
}

/* gpaste_typelib_ok -- true when GPaste-2.typelib sits somewhere GIRepository
 * actually searches. That is the introspection libdir it was built with, NOT
 * anything reachable via XDG_DATA_DIRS - which is exactly the trap a /usr/local
 * prefix falls into. Probing the system libdirs directly beats parsing that out
 * of gobject-introspection, and covers multiarch and plain layouts both. */
static int gpaste_typelib_ok(void) {
    struct utsname u;
    Str path;
    int ok = 0;
    size_t i;

    if (uname(&u) != 0) u.machine[0] = '\0';
    for (i = 0; i < 3 && !ok; i++) {
        str_init(&path);
        str_addz(&path, "/usr/lib");
        if (i == 0)      { str_addc(&path, '/'); str_addz(&path, u.machine); str_addz(&path, "-linux-gnu"); }
        else if (i == 1) { str_addz(&path, "64"); }
        str_addz(&path, "/girepository-1.0/GPaste-2.typelib");
        ok = file_exists(str_text(&path));
        str_free(&path);
    }
    return ok;
}

/* tag_newer -- `sort -V | tail -n 1` over two vN.N.N tags: which is the later
 * release. Dotted numeric compare, leading v ignored. */
static int tag_newer(const char *a, const char *b) {
    const char *p = a, *q = b;

    if (*p == 'v') p++;
    if (*q == 'v') q++;
    for (;;) {
        long x = atol(p), y = atol(q);
        if (x != y) return x > y;
        p = strchr(p, '.'); q = strchr(q, '.');
        if (p == NULL) return 0;                       /* equal, or a is shorter */
        if (q == NULL) return 1;
        p++; q++;
    }
}

/* gpaste_tag -- newest upstream tag on that GNOME major (v50.7), falling back
 * to the newest tag overall if the major is not covered yet. */
static void gpaste_tag(Str *out, const char *major) {
    Str json, want;
    const char *p;

    str_init(&json);
    (void)osr_fetch_buffer(&json,
        "https://api.github.com/repos/" GPASTE_REPO "/tags?per_page=100");
    str_init(&want);
    str_addc(&want, 'v');
    str_addz(&want, major);
    str_addc(&want, '.');
    str_reset(out);

    /* Every `"name": "v..."` in the payload, not just the first: the sh version
     * split the JSON on commas for exactly that reason. */
    p = str_text(&json);
    while ((p = strstr(p, "\"name\"")) != NULL) {
        Str tag;
        const char *q = p + 6;
        p = q;
        while (*q != '\0' && is_space(*q)) q++;
        if (*q != ':') continue;
        q++;
        while (*q != '\0' && is_space(*q)) q++;
        if (*q != '"') continue;
        q++;
        if (*q != 'v') continue;
        str_init(&tag);
        while (*q != '\0' && *q != '"') str_addc(&tag, *q++);
        if (strncmp(str_text(&tag), str_text(&want), want.len) == 0 &&
            (out->len == 0 || tag_newer(str_text(&tag), str_text(out)))) {
            str_reset(out);
            str_addz(out, str_text(&tag));
        }
        str_free(&tag);
    }
    str_free(&json);
    str_free(&want);
    if (out->len == 0) tag_of(out, GPASTE_REPO);
}

/* gpaste_remove_distro -- the distro package owns the same D-Bus names,
 * gsettings schema and extension UUID as the build. Two GPastes is one too many
 * -- and for the duplicate extension UUID the outcome is a coin toss over which
 * copy the Shell loads -- so the old one goes before the new one lands. */
static void gpaste_remove_distro(void) {
    static const char *const names[] = {
        "gpaste-2", "libgpaste-2", "libgpaste-2-common", "gir1.2-gpaste-2",
        "gnome-shell-extension-gpaste", NULL
    };
    Str raw, installed;
    char *argv[10];
    size_t i, argc = 0, pos = 0;
    Line l;

    if (!osr_have_cmd("dpkg-query")) return;
    argv[argc++] = (char *)"dpkg-query"; argv[argc++] = (char *)"-W";
    argv[argc++] = (char *)"-f"; argv[argc++] = (char *)"${Package} ${Status}\n";
    for (i = 0; names[i] != NULL; i++) argv[argc++] = (char *)names[i];
    argv[argc] = NULL;

    str_init(&raw);
    (void)osr_run_capture(argv, &raw);
    str_init(&installed);
    while (next_line(str_text(&raw), raw.len, &pos, &l)) {
        static const char tail[] = " install ok installed";
        if (l.len > sizeof(tail) - 1 &&
            strncmp(l.start + l.len - (sizeof(tail) - 1), tail, sizeof(tail) - 1) == 0) {
            if (installed.len > 0) str_addc(&installed, ' ');
            str_add(&installed, l.start, l.len - (sizeof(tail) - 1));
        }
    }
    str_free(&raw);

    if (installed.len > 0) {
        char **rm = (char **)calloc(installed.len + 5, sizeof(char *));
        size_t n = 0;
        char *p = installed.p;
        if (rm == NULL) osr_die_oom();
        osr_infof("removing the distro GPaste (%s )", str_text(&installed));
        rm[n++] = (char *)"apt-get"; rm[n++] = (char *)"remove"; rm[n++] = (char *)"-y";
        while (*p != '\0') {
            while (*p == ' ') *p++ = '\0';
            if (*p == '\0') break;
            rm[n++] = p;
            while (*p != '\0' && *p != ' ') p++;
        }
        rm[n] = NULL;
        if (osr_run_root(rm) != 0)
            osr_warn("could not remove the distro GPaste - the build may collide with it");
        free(rm);
    }
    str_free(&installed);
}

/* gpaste_clear_usr_local -- an earlier build of this module used a /usr/local
 * prefix (see the typelib note above for why it cannot work). Left in place it
 * is worse than useless: /usr/local/bin precedes /usr/bin on PATH, so the broken
 * gpaste-client keeps winning, and /usr/local/share is ahead of /usr/share in
 * XDG_DATA_DIRS, so the Shell keeps loading the extension that cannot find its
 * typelib. Every path GPaste installs there carries "gpaste" in its name, which
 * is what makes this narrow enough to delete. */
static void gpaste_clear_usr_local(void) {
    static const char *const dirs[] = { "bin", "libexec", "lib", "include", "share", NULL };
    size_t i;

    if (access("/usr/local/bin/gpaste-client", X_OK) != 0) return;
    osr_info("removing the earlier /usr/local GPaste (wrong prefix for the typelib)");
    for (i = 0; dirs[i] != NULL; i++) {
        Str path;
        char *argv[9];

        str_init(&path);
        str_addz(&path, "/usr/local/");
        str_addz(&path, dirs[i]);
        if (dir_exists(str_text(&path))) {
            argv[0] = (char *)"find"; argv[1] = path.p; argv[2] = (char *)"-iname";
            argv[3] = (char *)"*gpaste*"; argv[4] = (char *)"-depth";
            argv[5] = (char *)"-exec"; argv[6] = (char *)"rm"; argv[7] = (char *)"-rf";
            argv[8] = NULL;
            {
                char *full[11];
                size_t n;
                for (n = 0; argv[n] != NULL; n++) full[n] = argv[n];
                full[n++] = (char *)"{}";
                full[n++] = (char *)"+";
                full[n] = NULL;
                (void)osr_run_root_quiet(full);
            }
        }
        str_free(&path);
    }
}

/* provide_gpaste -- build GPaste from source, on the branch matching
 * gnome-shell.
 *
 * Idempotency goes BEYOND the caller's probe (§2), twice over: the probe looks
 * for a `gpaste` binary that upstream never installs (it is gpaste-client), and
 * an old distro GPaste would satisfy any name-only check while being exactly the
 * thing that has to go. So the builder compares majors itself -- and requires
 * the typelib to be FINDABLE too, or a box left in the wrong-prefix state would
 * skip its way out of ever being repaired.
 *
 * -Dvapi=false drops valac: nothing here consumes the Vala bindings.
 * Introspection stays ON - the shell extension imports GPaste through GIR and is
 * dead without the typelib. The X keybinder stays ON too: it is the fallback
 * that X11 sessions without a live extension need, and once the extension
 * matches the Shell it is never engaged. */
static int provide_gpaste(void) {
    static const char *const deps[] = { "build", "gpaste-build-deps", NULL };
    Str major, client, tag, tmp, tar_path, url, src, bld, pc, libdir, jobs;
    struct utsname u;
    char *argv[16];

    str_init(&major);
    if (!tool_major(&major, "gnome-shell", "--version"))
        osr_die("gnome-shell not found - GPaste is a GNOME Shell clipboard manager");

    str_init(&client);
    if (tool_major(&client, "gpaste-client", "version") &&
        strcmp(str_text(&client), str_text(&major)) == 0 && gpaste_typelib_ok()) {
        Str full;
        char *v[3];
        str_init(&full);
        v[0] = (char *)"gpaste-client"; v[1] = (char *)"version"; v[2] = NULL;
        (void)osr_run_capture(v, &full);
        str_trim_trailing(&full, '\n');
        osr_infof("GPaste %s already matches GNOME Shell %s - skipping the source build",
                  str_text(&full), str_text(&major));
        str_free(&full); str_free(&client); str_free(&major);
        return 1;
    }
    str_free(&client);

    gpaste_remove_distro();
    gpaste_clear_usr_local();
    pkg(deps);

    str_init(&tag);
    gpaste_tag(&tag, str_text(&major));
    osr_infof("building GPaste %s for GNOME Shell %s", str_text(&tag), str_text(&major));

    str_init(&tmp); str_init(&tar_path); str_init(&url);
    str_init(&src); str_init(&bld); str_init(&pc); str_init(&libdir); str_init(&jobs);
    if (!make_tmp_dir(&tmp)) osr_die("failed to create a temporary directory");
    str_addz(&tar_path, str_text(&tmp));
    str_addz(&tar_path, "/gpaste.tar.gz");
    str_addz(&url, "https://github.com/" GPASTE_REPO "/archive/refs/tags/");
    str_addz(&url, str_text(&tag));
    str_addz(&url, ".tar.gz");
    if (!osr_fetch_download(str_text(&url), tar_path.p, 0)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to download GPaste %s", str_text(&tag));
    }
    argv[0] = (char *)"tar"; argv[1] = (char *)"-xf"; argv[2] = tar_path.p;
    argv[3] = (char *)"-C"; argv[4] = tmp.p; argv[5] = NULL;
    if (!run_ok(argv)) {
        rm_rf(str_text(&tmp));
        osr_die("failed to extract GPaste %s", str_text(&tag));
    }
    str_addz(&src, str_text(&tmp));
    str_addz(&src, "/GPaste-");
    str_addz(&src, str_text(&tag) + (str_text(&tag)[0] == 'v' ? 1 : 0));
    {
        Str meson;
        int ok;
        str_init(&meson);
        str_addz(&meson, str_text(&src));
        str_addz(&meson, "/meson.build");
        ok = file_exists(str_text(&meson));
        str_free(&meson);
        if (!ok) {
            rm_rf(str_text(&tmp));
            osr_die("no meson.build in the GPaste tarball - its layout changed");
        }
    }
    str_addz(&bld, str_text(&src));
    str_addz(&bld, "/build");

    /* Debian/Ubuntu put libs and typelibs under a multiarch libdir. Only pass it
     * when that dir is really there - the point is to land beside the system's
     * own typelibs, so the system's own layout is the thing worth copying. */
    if (uname(&u) == 0) {
        Str probe;
        str_init(&probe);
        str_addz(&probe, "/usr/lib/");
        str_addz(&probe, u.machine);
        str_addz(&probe, "-linux-gnu");
        if (dir_exists(str_text(&probe))) {
            str_addz(&libdir, "--libdir=lib/");
            str_addz(&libdir, u.machine);
            str_addz(&libdir, "-linux-gnu");
        }
        str_free(&probe);
    }

    pkgconfig_env(&pc);
    {
        size_t n = 0;
        argv[n++] = (char *)"env"; argv[n++] = pc.p;
        argv[n++] = (char *)"meson"; argv[n++] = (char *)"setup";
        argv[n++] = bld.p; argv[n++] = src.p;
        argv[n++] = (char *)"--prefix=/usr";
        if (libdir.len > 0) argv[n++] = libdir.p;
        argv[n++] = (char *)"--buildtype=release";
        argv[n++] = (char *)"-Dvapi=false";
        argv[n++] = (char *)"-Ddbus-services-dir=/usr/share/dbus-1/services";
        argv[n++] = (char *)"-Dcontrol-center-keybindings-dir=/usr/share/gnome-control-center/keybindings";
        argv[n++] = (char *)"-Dsystemd-user-unit-dir=/usr/lib/systemd/user";
        argv[n] = NULL;
        if (osr_run(argv) != 0) {
            rm_rf(str_text(&tmp));
            osr_die("GPaste meson configure failed");
        }
    }
    build_jobs(&jobs);
    argv[0] = (char *)"meson"; argv[1] = (char *)"compile"; argv[2] = (char *)"-C";
    argv[3] = bld.p; argv[4] = (char *)"-j"; argv[5] = jobs.p; argv[6] = NULL;
    if (osr_run(argv) != 0) {
        rm_rf(str_text(&tmp));
        osr_die("GPaste build failed");
    }
    argv[0] = (char *)"meson"; argv[1] = (char *)"install"; argv[2] = (char *)"-C";
    argv[3] = bld.p; argv[4] = NULL;
    if (osr_run_root(argv) != 0) {
        rm_rf(str_text(&tmp));
        osr_die("GPaste install failed");
    }
    /* libgpaste lands in a prefix the loader may not scan by default. */
    argv[0] = (char *)"ldconfig"; argv[1] = NULL;
    (void)osr_run_root_quiet(argv);
    rm_rf(str_text(&tmp));

    str_free(&major); str_free(&tag); str_free(&tmp); str_free(&tar_path);
    str_free(&url); str_free(&src); str_free(&bld); str_free(&pc);
    str_free(&libdir); str_free(&jobs);
    if (!osr_have_cmd("gpaste-client"))
        osr_die("GPaste installed but gpaste-client is not on PATH");
    return 1;
}

/* --- the registry ---------------------------------------------------------- */

typedef struct {
    const char *name;
    int (*fn)(void);
} Builder;

static const Builder builders[] = {
    { "provide_yazi_bin",          provide_yazi_bin },
    { "provide_chafa",             provide_chafa },
    { "provide_ueberzugpp",        provide_ueberzugpp },
    { "provide_paru",              provide_paru },
    { "provide_zig",               provide_zig },
    { "provide_ghostty_copr",      provide_ghostty_copr },
    { "provide_ghostty_deb",       provide_ghostty_deb },
    { "provide_ghostty",           provide_ghostty },
    { "provide_wezterm",           provide_wezterm },
    { "provide_gh_tarball",        provide_gh_tarball },
    { "provide_btop_tarball",      provide_btop_tarball },
    { "provide_lsd_tarball",       provide_lsd_tarball },
    { "provide_fzf",               provide_fzf },
    { "provide_fastfetch_tarball", provide_fastfetch_tarball },
    { "provide_fastfetch_deb",     provide_fastfetch_deb },
    { "provide_lsd_deb",           provide_lsd_deb },
    { "provide_thunderbird_tarball", provide_thunderbird_tarball },
    { "provide_yandex_browser",    provide_yandex_browser },
    { "provide_proteus",           provide_proteus },
    { "provide_betterlockscreen",  provide_betterlockscreen },
    { "provide_autotiling",        provide_autotiling },
    { "provide_datagrip",          provide_datagrip },
    { "provide_telegram",          provide_telegram },
    { "provide_yandex_browser_deb", provide_yandex_browser_deb },
    { "provide_amneziavpn",        provide_amneziavpn },
    { "provide_gpaste",            provide_gpaste }
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

    if (osr_theme_only()) return osr_theme_only_skip("provide_*");
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
