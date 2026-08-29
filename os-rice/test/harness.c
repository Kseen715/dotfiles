/* test/harness.c -- implementation of test/harness.h.
 *
 * Unity-built: a test #includes this file, so nothing here is exported
 * and the whole thing compiles once per test binary. That is deliberate --
 * these tests link no lib object at all, and the only thing they know about
 * the harness core is the path build/osr and the argv it accepts.
 *
 * C89 + POSIX (fork/execve/mkdtemp/symlink). No snprintf: the rest of the tree
 * composes strings with a buffer type rather than a printf family C89 does not
 * have, and this file follows it.
 */
#define _POSIX_C_SOURCE 200809L

#include "harness.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* --- fatal ------------------------------------------------------------
 *
 * A sandbox that cannot be built is not a failing test, it is a test that
 * never ran, and reporting it as a FAIL would put a wrong cause in front of
 * whoever reads the run. Say what broke and stop.
 */
static void h_die(const char *what) {
    fprintf(stderr, "  harness: %s", what);
    if (errno != 0) fprintf(stderr, " (%s)", strerror(errno));
    fputc('\n', stderr);
    exit(2);
}

/* --- HStr ------------------------------------------------------------- */

void hs_init(HStr *s) {
    s->p = NULL;
    s->len = 0;
    s->cap = 0;
}

static void hs_grow(HStr *s, size_t need) {
    size_t cap = s->cap ? s->cap : 64;
    char *np;
    if (s->len + need + 1 <= s->cap) return;
    while (cap < s->len + need + 1) cap *= 2;
    np = (char *)realloc(s->p, cap);
    if (np == NULL) h_die("out of memory");
    s->p = np;
    s->cap = cap;
}

void hs_reset(HStr *s) {
    s->len = 0;
    if (s->p != NULL) s->p[0] = '\0';
}

void hs_free(HStr *s) {
    free(s->p);
    hs_init(s);
}

void hs_add(HStr *s, const char *z) {
    size_t n;
    if (z == NULL) return;
    n = strlen(z);
    hs_grow(s, n);
    memcpy(s->p + s->len, z, n);
    s->len += n;
    s->p[s->len] = '\0';
}

void hs_addc(HStr *s, char c) {
    hs_grow(s, 1);
    s->p[s->len++] = c;
    s->p[s->len] = '\0';
}

void hs_addn(HStr *s, long n) {
    char buf[32];
    char *q = buf + sizeof(buf) - 1;
    int neg = n < 0;
    unsigned long u = neg ? (unsigned long)(-(n + 1)) + 1UL : (unsigned long)n;
    *q = '\0';
    do {
        *--q = (char)('0' + (u % 10UL));
        u /= 10UL;
    } while (u != 0UL);
    if (neg) *--q = '-';
    hs_add(s, q);
}

const char *hs_text(const HStr *s) { return s->p != NULL ? s->p : ""; }

void hs_path(HStr *s, const char *dir, const char *leaf) {
    hs_reset(s);
    hs_add(s, dir);
    hs_addc(s, '/');
    hs_add(s, leaf);
}

/* h_dup -- strdup, which C89 does not have. */
static char *h_dup(const char *z) {
    size_t n = strlen(z) + 1;
    char *p = (char *)malloc(n);
    if (p == NULL) h_die("out of memory");
    memcpy(p, z, n);
    return p;
}

/* --- files ------------------------------------------------------------ */

/* h_write -- create `path` with `contents` and chmod it. */
static void h_write(const char *path, const char *contents, int mode) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) h_die(path);
    if (contents != NULL && *contents != '\0') {
        if (fwrite(contents, 1, strlen(contents), f) != strlen(contents)) h_die(path);
    }
    if (fclose(f) != 0) h_die(path);
    if (chmod(path, (mode_t)mode) != 0) h_die(path);
}

/* h_slurp -- the whole file, or "" when it does not exist. The caller frees. */
static char *h_slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    HStr s;
    int c;
    hs_init(&s);
    if (f == NULL) {
        hs_add(&s, "");
        return s.p != NULL ? s.p : h_dup("");
    }
    while ((c = fgetc(f)) != EOF) hs_addc(&s, (char)c);
    fclose(f);
    return s.p != NULL ? s.p : h_dup("");
}

/* h_mkdir_p -- mkdir every component of an absolute path. */
static void h_mkdir_p(const char *path) {
    HStr acc;
    const char *p = path;
    hs_init(&acc);
    if (*p == '/') {
        hs_addc(&acc, '/');
        p++;
    }
    while (*p != '\0') {
        const char *slash = strchr(p, '/');
        size_t n = slash != NULL ? (size_t)(slash - p) : strlen(p);
        if (n > 0) {
            size_t i;
            for (i = 0; i < n; i++) hs_addc(&acc, p[i]);
            if (mkdir(hs_text(&acc), 0755) != 0 && errno != EEXIST) h_die(hs_text(&acc));
            hs_addc(&acc, '/');
        }
        if (slash == NULL) break;
        p = slash + 1;
    }
    hs_free(&acc);
}

/* h_rm_rf -- the sandbox teardown. Depth-first, and it never follows a
 * symlink: the sandbox PATH is mostly symlinks into /usr/bin, and a teardown
 * that recursed through one would delete the host's coreutils.
 *
 * Written out rather than delegated to `rm -rf`, because a teardown that needs
 * a working shell is exactly the thing a test tearing apart its own PATH
 * cannot rely on -- and that is how a suite leaves litter in /tmp.
 */
static void h_rm_rf(const char *path) {
    DIR *d;
    struct dirent *e;
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (!S_ISDIR(st.st_mode)) {
        unlink(path);
        return;
    }
    d = opendir(path);
    if (d == NULL) return;
    while ((e = readdir(d)) != NULL) {
        HStr child;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        hs_init(&child);
        hs_path(&child, path, e->d_name);
        h_rm_rf(hs_text(&child));
        hs_free(&child);
    }
    closedir(d);
    rmdir(path);
}

/* --- sandbox ---------------------------------------------------------- */

/* h_tree_root -- the os-rice tree, absolute.
 *
 * A test's working directory is test/unit_c (nob.c's run_test), so the tree is
 * two levels up. $OSR_TEST_ROOT overrides it, which is how a test can be run
 * by hand from anywhere. */
static void h_tree_root(HStr *out) {
    const char *override = getenv("OSR_TEST_ROOT");
    char cwd[4096];
    hs_reset(out);
    if (override != NULL && *override != '\0') {
        hs_add(out, override);
        return;
    }
    if (getcwd(cwd, sizeof(cwd)) == NULL) h_die("getcwd");
    hs_add(out, cwd);
    hs_add(out, "/../..");
}

/* h_real_tools -- the programs the code under test cannot run without.
 *
 * `command -v` answers a BUILTIN with its bare name (test, printf, true), so a
 * bare answer is looked up on disk instead: linking the bare name would make a
 * dangling symlink and every use of it would fail as 127, an agreement that
 * proves nothing. `test` really is called as a program here, by `as_user test -x`.
 */
static const char *h_real_tools[] = {
    "sh", "env", "cat", "cut", "grep", "sed", "awk", "tr", "head", "tail",
    "printf", "id", "mktemp", "rm", "cp", "mv", "ln", "mkdir", "rmdir",
    "tee", "sort", "uniq", "od", "wc", "dirname", "basename", "sleep",
    "kill", "chmod", "touch", "test", "true", "false", "date", "find",
    "readlink", "stat", "diff", "xargs", "tar", "gzip", "install", NULL
};

static const char *h_search_dirs[] = {
    "/usr/bin", "/bin", "/usr/local/bin", "/usr/sbin", "/sbin", NULL
};

void osr_sb_real(OsrSandbox *sb, const char *name) {
    HStr src;
    HStr dst;
    int i;
    hs_init(&src);
    hs_init(&dst);
    hs_path(&dst, hs_text(&sb->bin), name);
    for (i = 0; h_search_dirs[i] != NULL; i++) {
        hs_path(&src, h_search_dirs[i], name);
        if (access(hs_text(&src), X_OK) == 0) {
            unlink(hs_text(&dst));
            if (symlink(hs_text(&src), hs_text(&dst)) != 0) h_die(hs_text(&dst));
            break;
        }
    }
    hs_free(&src);
    hs_free(&dst);
}

void osr_sb_env(OsrSandbox *sb, const char *name, const char *value) {
    HStr e;
    size_t nlen = strlen(name);
    int i;
    hs_init(&e);
    hs_add(&e, name);
    hs_addc(&e, '=');
    hs_add(&e, value);
    for (i = 0; i < sb->env_n; i++) {
        if (strncmp(sb->env[i], name, nlen) == 0 && sb->env[i][nlen] == '=') {
            free(sb->env[i]);
            sb->env[i] = h_dup(hs_text(&e));
            hs_free(&e);
            return;
        }
    }
    if (sb->env_n >= OSR_SB_MAX_ENV - 1) h_die("too many environment entries");
    sb->env[sb->env_n++] = h_dup(hs_text(&e));
    sb->env[sb->env_n] = NULL;
    hs_free(&e);
}

void osr_sb_drop(OsrSandbox *sb, const char *line) {
    if (sb->drop_n >= OSR_SB_MAX_DROP) h_die("too many drop patterns");
    sb->drop[sb->drop_n++] = h_dup(line);
}

void osr_sb_init(OsrSandbox *sb) {
    char tmpl[] = "/tmp/osr-parity-XXXXXX";
    const char *dir;
    int i;

    memset(sb, 0, sizeof(*sb));
    hs_init(&sb->root);
    hs_init(&sb->bin);
    hs_init(&sb->home);
    hs_init(&sb->log);
    hs_init(&sb->out);
    hs_init(&sb->err);
    hs_init(&sb->osr_root);
    hs_init(&sb->osr_lib);
    hs_init(&sb->osr_exe);

    dir = mkdtemp(tmpl);
    if (dir == NULL) h_die("mkdtemp");
    hs_add(&sb->root, dir);
    hs_path(&sb->bin, dir, "bin");
    hs_path(&sb->home, dir, "home");
    hs_path(&sb->log, dir, "log");
    hs_path(&sb->out, dir, "out");
    hs_path(&sb->err, dir, "err");
    h_mkdir_p(hs_text(&sb->bin));
    h_mkdir_p(hs_text(&sb->home));

    h_tree_root(&sb->osr_root);
    hs_path(&sb->osr_lib, hs_text(&sb->osr_root), "lib");
    hs_path(&sb->osr_exe, hs_text(&sb->osr_root), "build/osr");

    for (i = 0; h_real_tools[i] != NULL; i++) osr_sb_real(sb, h_real_tools[i]);

    /* sudo records the escalation and then runs the command, so `as_root
     * apt-get` in the frozen shell and osr_run_root() in C leave the same two
     * lines. It is a stub rather than the real thing for the obvious reason. */
    osr_sb_stub_body(sb, "sudo",
        "printf 'sudo %s\\n' \"$*\" >>\"$LOG\"\n"
        "[ \"$1\" = \"-u\" ] && shift 2\n"
        "exec \"$@\"\n");

    /* The baseline environment. Everything the harness core reads about the
     * box is pinned here, so a scenario states only what it changes. */
    osr_sb_env(sb, "PATH", hs_text(&sb->bin));
    osr_sb_env(sb, "LOG", hs_text(&sb->log));
    osr_sb_env(sb, "HOME", hs_text(&sb->home));
    osr_sb_env(sb, "OSR_HOME", hs_text(&sb->home));
    osr_sb_env(sb, "OSR_ROOT", hs_text(&sb->osr_root));
    osr_sb_env(sb, "OSR_LIB", hs_text(&sb->osr_lib));
    osr_sb_env(sb, "OSR_USER", "tester");
    osr_sb_env(sb, "OSR_DISTRO", "ubuntu");
    osr_sb_env(sb, "OSR_ID_LIKE", "debian");
    osr_sb_env(sb, "OSR_CODENAME", "noble");
    osr_sb_env(sb, "OSR_VERSION_ID", "24.04");
    osr_sb_env(sb, "OSR_ARCH", "x86_64");
    osr_sb_env(sb, "OSR_VERBOSE", "1");
    osr_sb_env(sb, "NO_COLOR", "1");
    osr_sb_env(sb, "TERM", "dumb");
    {
        HStr runlog;
        hs_init(&runlog);
        hs_path(&runlog, hs_text(&sb->root), "run.log");
        osr_sb_env(sb, "OSR_LOG", hs_text(&runlog));
        hs_free(&runlog);
    }

    osr_sb_reset(sb);
}

void osr_sb_free(OsrSandbox *sb) {
    int i;
    h_rm_rf(hs_text(&sb->root));
    for (i = 0; i < sb->env_n; i++) free(sb->env[i]);
    for (i = 0; i < sb->drop_n; i++) free(sb->drop[i]);
    for (i = 0; i < sb->mask_n; i++) free(sb->mask[i]);
    hs_free(&sb->root);
    hs_free(&sb->bin);
    hs_free(&sb->home);
    hs_free(&sb->log);
    hs_free(&sb->out);
    hs_free(&sb->err);
    hs_free(&sb->osr_root);
    hs_free(&sb->osr_lib);
    hs_free(&sb->osr_exe);
}

void osr_sb_stub_body(OsrSandbox *sb, const char *name, const char *body) {
    HStr path;
    HStr script;
    hs_init(&path);
    hs_init(&script);
    hs_path(&path, hs_text(&sb->bin), name);
    hs_add(&script, "#!/bin/sh\n");
    hs_add(&script, body);
    unlink(hs_text(&path));
    h_write(hs_text(&path), hs_text(&script), 0755);
    hs_free(&path);
    hs_free(&script);
}

void osr_sb_stub(OsrSandbox *sb, const char *name, int code) {
    HStr body;
    hs_init(&body);
    hs_add(&body, "printf '");
    hs_add(&body, name);
    hs_add(&body, " %s\\n' \"$*\" >>\"$LOG\"\nexit ");
    hs_addn(&body, (long)code);
    hs_addc(&body, '\n');
    osr_sb_stub_body(sb, name, hs_text(&body));
    hs_free(&body);
}

void osr_sb_mkdir(OsrSandbox *sb, const char *rel) {
    HStr p;
    hs_init(&p);
    hs_path(&p, hs_text(&sb->root), rel);
    h_mkdir_p(hs_text(&p));
    hs_free(&p);
}

void osr_sb_write(OsrSandbox *sb, const char *rel, const char *contents, int mode) {
    HStr p;
    HStr dir;
    const char *slash;
    hs_init(&p);
    hs_init(&dir);
    hs_path(&p, hs_text(&sb->root), rel);
    slash = strrchr(hs_text(&p), '/');
    if (slash != NULL) {
        hs_add(&dir, hs_text(&p));
        dir.p[slash - hs_text(&p)] = '\0';
        dir.len = (size_t)(slash - hs_text(&p));
        h_mkdir_p(hs_text(&dir));
    }
    h_write(hs_text(&p), contents, mode);
    hs_free(&p);
    hs_free(&dir);
}

void osr_sb_reset(OsrSandbox *sb) {
    h_write(hs_text(&sb->log), "", 0644);
}

/* --- running ---------------------------------------------------------- */

/* h_run -- fork, redirect stdout+stderr into sb->out, execve with the
 * sandbox environment and nothing else. Returns the exit status; a program
 * that could not be started is 127, which is what a shell would have said.
 */
/* h_stdin_path -- the file the next run reads as stdin, or "" for /dev/null.
 * Set by osr_sb_stdin and cleared after one run, so a verb that reads stdin is
 * fed deliberately and nothing else inherits the terminal. */
static HStr h_stdin_file;
static int h_stdin_ready = 0;

void osr_sb_stdin(OsrSandbox *sb, const char *text) {
    if (!h_stdin_ready) { hs_init(&h_stdin_file); h_stdin_ready = 1; }
    osr_sb_write(sb, ".stdin", text, 0644);
    hs_reset(&h_stdin_file);
    hs_path(&h_stdin_file, hs_text(&sb->root), ".stdin");
}

static int h_run(OsrSandbox *sb, char **argv) {
    pid_t pid;
    int status;
    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid < 0) h_die("fork");
    if (pid == 0) {
        /* Two files, not one with stderr dup'd onto stdout: which stream a
         * line went to is itself part of what is being frozen. */
        if (freopen(hs_text(&sb->out), "wb", stdout) == NULL) _exit(127);
        if (freopen(hs_text(&sb->err), "wb", stderr) == NULL) _exit(127);
        /* stdin is /dev/null unless the scenario fed something in: a verb that
         * reads stdin must never reach the terminal running the suite. */
        if (h_stdin_ready && hs_text(&h_stdin_file)[0] != '\0') {
            if (freopen(hs_text(&h_stdin_file), "rb", stdin) == NULL) _exit(127);
        } else {
            if (freopen("/dev/null", "rb", stdin) == NULL) _exit(127);
        }
        execve(argv[0], argv, sb->env);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) h_die("waitpid");
    if (h_stdin_ready) hs_reset(&h_stdin_file);   /* one run, one feeding */
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

/* h_collect -- a NULL-terminated va_list into argv, after `argv0`. */
static void h_collect(char **argv, int *n, int max, const char *argv0, va_list ap) {
    const char *a;
    *n = 0;
    argv[(*n)++] = (char *)argv0;
    while ((a = va_arg(ap, const char *)) != NULL) {
        if (*n >= max - 1) h_die("too many arguments");
        argv[(*n)++] = (char *)a;
    }
    argv[*n] = NULL;
}

int osr_sb_run_core(OsrSandbox *sb, ...) {
    char *argv[32];
    int n;
    va_list ap;
    va_start(ap, sb);
    h_collect(argv, &n, 32, hs_text(&sb->osr_exe), ap);
    va_end(ap);
    return h_run(sb, argv);
}

/* --- normalize ---------------------------------------------- */

static HStr h_norm_buf;
static HStr h_out_buf;
static int h_bufs_ready = 0;

static void h_bufs_init(void) {
    if (h_bufs_ready) return;
    hs_init(&h_norm_buf);
    hs_init(&h_out_buf);
    h_bufs_ready = 1;
}

/* h_normalize -- the log with every dropped line removed.
 *
 * Line-oriented and exact-match, not substring: a filter that matched loosely
 * could hide a real difference in an argument, which is the only thing these
 * logs carry.
 */
static const char *h_normalize(OsrSandbox *sb, const char *text) {
    const char *p = text;
    h_bufs_init();
    hs_reset(&h_norm_buf);
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        size_t n = nl != NULL ? (size_t)(nl - p) : strlen(p);
        int drop = 0;
        int i;
        for (i = 0; i < sb->drop_n; i++) {
            if (strlen(sb->drop[i]) == n && strncmp(p, sb->drop[i], n) == 0) {
                drop = 1;
                break;
            }
        }
        if (!drop) {
            size_t k;
            for (k = 0; k < n; k++) hs_addc(&h_norm_buf, p[k]);
            hs_addc(&h_norm_buf, '\n');
        }
        if (nl == NULL) break;
        p = nl + 1;
    }
    return hs_text(&h_norm_buf);
}

const char *osr_sb_log(OsrSandbox *sb) {
    char *raw = h_slurp(hs_text(&sb->log));
    h_bufs_init();
    /* Drops first, then the scrub: a dropped line is matched exactly, and
     * matching it against text that has already had paths rewritten would
     * make the drop list depend on where the sandbox happened to land. */
    hs_reset(&h_out_buf);
    hs_add(&h_out_buf, h_normalize(sb, raw));
    free(raw);
    {
        HStr clean;
        hs_init(&clean);
        hs_add(&clean, osr_sb_scrub(sb, hs_text(&h_out_buf)));
        hs_reset(&h_out_buf);
        hs_add(&h_out_buf, hs_text(&clean));
        hs_free(&clean);
    }
    return hs_text(&h_out_buf);
}

/* h_capture -- one of the two streams into a buffer of its own, so a caller
 * can hold stdout while it reads stderr. */
static const char *h_capture(HStr *buf, int *ready, const char *path) {
    char *raw;
    if (!*ready) {
        hs_init(buf);
        *ready = 1;
    }
    raw = h_slurp(path);
    hs_reset(buf);
    hs_add(buf, raw);
    free(raw);
    return hs_text(buf);
}

const char *osr_sb_capture(OsrSandbox *sb) {
    static HStr cap;
    static int ready = 0;
    return h_capture(&cap, &ready, hs_text(&sb->out));
}

const char *osr_sb_capture_err(OsrSandbox *sb) {
    static HStr cap;
    static int ready = 0;
    return h_capture(&cap, &ready, hs_text(&sb->err));
}

const char *osr_sb_capture_both(OsrSandbox *sb) {
    static HStr both;
    static int ready = 0;
    if (!ready) {
        hs_init(&both);
        ready = 1;
    }
    hs_reset(&both);
    hs_add(&both, osr_sb_capture(sb));
    hs_add(&both, osr_sb_capture_err(sb));
    return hs_text(&both);
}

/* --- assertions ------------------------------------------------------- */

static int h_pass = 0;
static int h_failed = 0;

/* h_color -- the suite's palette, handed down by the runner in
 * OSR_TEST_COLOR because every test blanks OSR_* on purpose. */
static const char *h_green(void) {
    const char *c = getenv("OSR_TEST_COLOR");
    return (c != NULL && *c != '\0') ? "\033[0;32m" : "";
}
static const char *h_red(void) {
    const char *c = getenv("OSR_TEST_COLOR");
    return (c != NULL && *c != '\0') ? "\033[0;31m" : "";
}
static const char *h_nc(void) {
    const char *c = getenv("OSR_TEST_COLOR");
    return (c != NULL && *c != '\0') ? "\033[0m" : "";
}

void osr_ok(const char *label) {
    h_pass++;
    printf("  %sok%s   %s\n", h_green(), h_nc(), label);
    fflush(stdout);
}

void osr_fail(const char *label, const char *detail) {
    h_failed++;
    fflush(stdout);
    if (detail != NULL && *detail != '\0') {
        fprintf(stderr, "  %sFAIL%s %s (%s)\n", h_red(), h_nc(), label, detail);
    } else {
        fprintf(stderr, "  %sFAIL%s %s\n", h_red(), h_nc(), label);
    }
    fflush(stderr);
}

int osr_finish(void) {
    printf("  %s--- %d passed, %d failed ---%s\n",
           h_failed == 0 ? h_green() : h_red(), h_pass, h_failed, h_nc());
    fflush(stdout);
    return h_failed == 0 ? 0 : 1;
}

void osr_assert_true(int cond, const char *label) {
    if (cond) osr_ok(label); else osr_fail(label, "expected true");
}

void osr_assert_eq(const char *expected, const char *actual, const char *label) {
    if (expected != NULL && actual != NULL && strcmp(expected, actual) == 0) {
        osr_ok(label);
    } else {
        HStr d;
        hs_init(&d);
        hs_add(&d, "expected '");
        hs_add(&d, expected != NULL ? expected : "(null)");
        hs_add(&d, "', got '");
        hs_add(&d, actual != NULL ? actual : "(null)");
        hs_addc(&d, '\'');
        osr_fail(label, hs_text(&d));
        hs_free(&d);
    }
}

void osr_assert_log(OsrSandbox *sb, const char *needle, const char *label) {
    if (strstr(osr_sb_log(sb), needle) != NULL) osr_ok(label);
    else osr_fail(label, needle);
}

void osr_refute_log(OsrSandbox *sb, const char *needle, const char *label) {
    if (strstr(osr_sb_log(sb), needle) == NULL) osr_ok(label);
    else osr_fail(label, needle);
}

void osr_assert_out(OsrSandbox *sb, const char *needle, const char *label) {
    if (strstr(osr_sb_capture(sb), needle) != NULL) osr_ok(label);
    else osr_fail(label, needle);
}

/* --- scrub + goldens -------------------------------------------------- */

/* h_replace_all -- every occurrence of `from` becomes `to`. */
static void h_replace_all(HStr *dst, const char *text, const char *from, const char *to) {
    size_t flen = strlen(from);
    const char *p = text;
    hs_reset(dst);
    if (flen == 0) {
        hs_add(dst, text);
        return;
    }
    while (*p != '\0') {
        const char *hit = strstr(p, from);
        if (hit == NULL) {
            hs_add(dst, p);
            return;
        }
        while (p < hit) hs_addc(dst, *p++);
        hs_add(dst, to);
        p += flen;
    }
}

/* h_apply_masks -- collapse what follows each registered prefix, up to the
 * next whitespace, to a single "X". */
static void h_apply_masks(OsrSandbox *sb, HStr *dst, const char *text) {
    const char *p = text;
    hs_reset(dst);
    if (sb->mask_n == 0) {
        hs_add(dst, text);
        return;
    }
    while (*p != '\0') {
        int i;
        int matched = 0;
        for (i = 0; i < sb->mask_n; i++) {
            size_t n = strlen(sb->mask[i]);
            if (strncmp(p, sb->mask[i], n) == 0) {
                hs_add(dst, sb->mask[i]);
                hs_addc(dst, 'X');
                p += n;
                while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n') p++;
                matched = 1;
                break;
            }
        }
        if (!matched) hs_addc(dst, *p++);
    }
}

void osr_sb_mask(OsrSandbox *sb, const char *prefix) {
    if (sb->mask_n >= OSR_SB_MAX_MASK) h_die("too many masks");
    sb->mask[sb->mask_n++] = h_dup(prefix);
}

const char *osr_sb_scrub(OsrSandbox *sb, const char *text) {
    static HStr a;
    static HStr b;
    static int ready = 0;
    if (!ready) {
        hs_init(&a);
        hs_init(&b);
        ready = 1;
    }
    /* Order matters: the sandbox root is the longest and most specific, and
     * $HOME lives inside it, so collapsing the root first leaves ROOT/home
     * rather than two placeholders fighting over the same bytes. */
    h_replace_all(&a, text, hs_text(&sb->root), "ROOT");
    h_replace_all(&b, hs_text(&a), hs_text(&sb->osr_root), "TREE");
    h_apply_masks(sb, &a, hs_text(&b));
    return hs_text(&a);
}

/* osr_sb_tree -- `find <rel> | sort`, with a symlink shown as `name -> target`.
 *
 * Freezes what a run LEFT BEHIND, which for some verbs is the whole of what
 * they did: the runit branch of enable_service makes one symlink and runs
 * nothing at all, so an argv log alone would call it a no-op. */
static void h_tree_walk(OsrSandbox *sb, const char *abs, const char *shown, HStr *out);

/* h_strcmp_p -- qsort over an array of char*. */
static int h_strcmp_p(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void h_tree_walk(OsrSandbox *sb, const char *abs, const char *shown, HStr *out) {
    DIR *d;
    struct dirent *e;
    char *names[512];
    int n = 0;
    int i;

    hs_add(out, shown);
    hs_addc(out, '\n');

    d = opendir(abs);
    if (d == NULL) return;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (n >= 512) break;
        names[n++] = h_dup(e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof(names[0]), h_strcmp_p);

    for (i = 0; i < n; i++) {
        HStr child_abs;
        HStr child_shown;
        struct stat st;
        hs_init(&child_abs);
        hs_init(&child_shown);
        hs_path(&child_abs, abs, names[i]);
        hs_path(&child_shown, shown, names[i]);
        if (lstat(hs_text(&child_abs), &st) == 0 && S_ISLNK(st.st_mode)) {
            char target[4096];
            ssize_t k = readlink(hs_text(&child_abs), target, sizeof(target) - 1);
            target[k > 0 ? (size_t)k : 0] = '\0';
            hs_add(out, hs_text(&child_shown));
            hs_add(out, " -> ");
            hs_add(out, target);
            hs_addc(out, '\n');
        } else if (lstat(hs_text(&child_abs), &st) == 0 && S_ISDIR(st.st_mode)) {
            h_tree_walk(sb, hs_text(&child_abs), hs_text(&child_shown), out);
        } else {
            hs_add(out, hs_text(&child_shown));
            hs_addc(out, '\n');
        }
        hs_free(&child_abs);
        hs_free(&child_shown);
        free(names[i]);
    }
}

const char *osr_sb_tree(OsrSandbox *sb, const char *rel) {
    static HStr tree;
    static int ready = 0;
    HStr abs;
    if (!ready) {
        hs_init(&tree);
        ready = 1;
    }
    hs_reset(&tree);
    hs_init(&abs);
    hs_path(&abs, hs_text(&sb->root), rel);
    h_tree_walk(sb, hs_text(&abs), rel, &tree);
    hs_free(&abs);
    return hs_text(&tree);
}


/* --- seeding a scenario's state -------------------------------------- */

void osr_sb_symlink(OsrSandbox *sb, const char *target_rel, const char *rel) {
    HStr target;
    HStr link;
    hs_init(&target);
    hs_init(&link);
    hs_path(&target, hs_text(&sb->root), target_rel);
    hs_path(&link, hs_text(&sb->root), rel);
    unlink(hs_text(&link));
    if (symlink(hs_text(&target), hs_text(&link)) != 0) h_die(hs_text(&link));
    hs_free(&target);
    hs_free(&link);
}

void osr_sb_rm(OsrSandbox *sb, const char *rel) {
    HStr p;
    hs_init(&p);
    hs_path(&p, hs_text(&sb->root), rel);
    h_rm_rf(hs_text(&p));
    hs_free(&p);
}

/* --- assertions over whole text --------------------------------------
 *
 * h_cmp is the shared body: compare, and on a mismatch name the first line
 * that differed rather than dumping both sides. A wall of context buries the
 * one argument that changed, which is the thing worth seeing.
 */
static void h_cmp(const char *expected, const char *actual, const char *label) {
    const char *a;
    const char *b;
    int line = 1;

    if (strcmp(expected, actual) == 0) {
        osr_ok(label);
        return;
    }
    osr_fail(label, NULL);
    a = expected;
    b = actual;
    while (*a != '\0' || *b != '\0') {
        const char *ae = strchr(a, '\n');
        const char *be = strchr(b, '\n');
        size_t an = ae != NULL ? (size_t)(ae - a) : strlen(a);
        size_t bn = be != NULL ? (size_t)(be - b) : strlen(b);
        if (an != bn || strncmp(a, b, an) != 0) {
            fprintf(stderr, "       line %d\n", line);
            fprintf(stderr, "       expected: %.*s\n", (int)an, a);
            fprintf(stderr, "       actual:   %.*s\n", (int)bn, b);
            /* OSR_TEST_DUMP -- print the whole actual text, not just the
             * first line that differed. The one line is what you want when a
             * known-good expectation breaks; the whole thing is what you want
             * when you are WRITING the expectation and need to see what the
             * run really did. Off by default so a CI failure stays readable. */
            if (getenv("OSR_TEST_DUMP") != NULL) {
                fprintf(stderr, "       --- actual, in full ---\n");
                fprintf(stderr, "%s", actual);
                fprintf(stderr, "       --- end ---\n");
            }
            fflush(stderr);
            return;
        }
        if (ae == NULL && be == NULL) break;
        a = ae != NULL ? ae + 1 : a + an;
        b = be != NULL ? be + 1 : b + bn;
        line++;
    }
}

void osr_assert_log_is(OsrSandbox *sb, const char *expected, const char *label) {
    h_cmp(expected, osr_sb_log(sb), label);
}

void osr_assert_log_empty(OsrSandbox *sb, const char *label) {
    const char *got = osr_sb_log(sb);
    if (*got == '\0') osr_ok(label);
    else h_cmp("", got, label);
}

void osr_assert_err(OsrSandbox *sb, const char *needle, const char *label) {
    if (strstr(osr_sb_scrub(sb, osr_sb_capture_err(sb)), needle) != NULL) osr_ok(label);
    else osr_fail(label, needle);
}

void osr_assert_out_is(OsrSandbox *sb, const char *expected, const char *label) {
    HStr held;
    hs_init(&held);
    hs_add(&held, osr_sb_scrub(sb, osr_sb_capture(sb)));
    h_cmp(expected, hs_text(&held), label);
    hs_free(&held);
}

void osr_assert_silent(OsrSandbox *sb, const char *label) {
    HStr held;
    int quiet;
    hs_init(&held);
    hs_add(&held, osr_sb_capture(sb));
    hs_add(&held, osr_sb_capture_err(sb));
    quiet = hs_text(&held)[0] == '\0';
    if (quiet) osr_ok(label);
    else osr_fail(label, hs_text(&held));
    hs_free(&held);
}

void osr_assert_tree_is(OsrSandbox *sb, const char *rel, const char *expected,
                        const char *label) {
    HStr held;
    hs_init(&held);
    hs_add(&held, osr_sb_scrub(sb, osr_sb_tree(sb, rel)));
    h_cmp(expected, hs_text(&held), label);
    hs_free(&held);
}

void osr_assert_link(OsrSandbox *sb, const char *rel, const char *target,
                     const char *label) {
    HStr path;
    struct stat st;
    char buf[4096];
    ssize_t n;

    hs_init(&path);
    hs_path(&path, hs_text(&sb->root), rel);
    if (lstat(hs_text(&path), &st) != 0) {
        osr_fail(label, "no such path");
    } else if (!S_ISLNK(st.st_mode)) {
        osr_fail(label, "exists but is not a symlink");
    } else {
        n = readlink(hs_text(&path), buf, sizeof(buf) - 1);
        buf[n > 0 ? (size_t)n : 0] = '\0';
        osr_assert_eq(target, osr_sb_scrub(sb, buf), label);
    }
    hs_free(&path);
}

void osr_assert_absent(OsrSandbox *sb, const char *rel, const char *label) {
    HStr path;
    struct stat st;
    hs_init(&path);
    hs_path(&path, hs_text(&sb->root), rel);
    /* lstat, not stat: a dangling symlink is something left behind, and a
     * check that followed it would call the leftover "absent". */
    if (lstat(hs_text(&path), &st) != 0) osr_ok(label);
    else osr_fail(label, "the path exists");
    hs_free(&path);
}

void osr_assert_rc(int actual, int expected, const char *label) {
    if (actual == expected) {
        osr_ok(label);
    } else {
        HStr d;
        hs_init(&d);
        hs_add(&d, "expected exit ");
        hs_addn(&d, (long)expected);
        hs_add(&d, ", got ");
        hs_addn(&d, (long)actual);
        osr_fail(label, hs_text(&d));
        hs_free(&d);
    }
}
