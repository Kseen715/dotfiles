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
