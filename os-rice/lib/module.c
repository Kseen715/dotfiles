/* lib/module.c -- the implementation of lib/module.h, the API a Linux module
 * written in C is allowed to use.
 *
 * Everything here is the C form of something a .sh module called: run_step,
 * pkg_install, enable_service, as_root/as_user, backup_copy, ensure_line. The
 * sh versions stay where they are (lib/pkg.sh, lib/service.sh, lib/config.sh)
 * because ~118 shell modules still call them; these are the same behaviors
 * for the modules that have moved to C.
 *
 * Package handling is the native path only -- see osr_pkg_install's comment in
 * lib/module.h for what that deliberately excludes.
 *
 * C89 + POSIX.
 */
#define _XOPEN_SOURCE 700

#include "common.h"
#include "module.h"
#include "ui.h"
#include "render.h"

#include <fcntl.h>
#include <pwd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* --- the facts ------------------------------------------------------------ */

const char *osr_mod_root(void)      { return env_str("OSR_ROOT", "."); }
const char *osr_mod_dotfiles(void)  { return env_str("OSR_DOTFILES", ".."); }
const char *osr_mod_user(void)      { return env_str("OSR_USER", ""); }
const char *osr_mod_home(void)      { return env_str("OSR_HOME", env_str("HOME", "")); }
const char *osr_mod_theme(void)     { return env_str("OSR_THEME", ""); }
const char *osr_mod_theme_dir(void) { return env_str("OSR_THEME_DIR", ""); }
const char *osr_mod_pkg(void)       { return env_str("OSR_PKG", ""); }
const char *osr_mod_distro(void)    { return env_str("OSR_DISTRO", ""); }
const char *osr_mod_init(void)      { return env_str("OSR_INIT", ""); }

/* --- saying things -------------------------------------------------------- */

static void say(void (*emit)(const char *), const char *fmt, va_list ap) {
    char buf[2048];
    vsprintf(buf, fmt, ap);
    emit(buf);
}

void osr_infof(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    say(osr_info, fmt, ap);
    va_end(ap);
}

void osr_warnf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    say(osr_warn, fmt, ap);
    va_end(ap);
}

void osr_successf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    say(osr_success_line, fmt, ap);
    va_end(ap);
}

/* osr_die -- lib/log.sh's error(): print, then end the run. In sh only the
 * shell could do the exit; here the module IS the process. */
void osr_die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    say(osr_error_line, fmt, ap);
    va_end(ap);
    exit(1);
}

/* --- running things ------------------------------------------------------- */

/* whoami -- the account this process runs as. */
static const char *whoami(void) {
    struct passwd *pw = getpwuid(getuid());
    return (pw != NULL && pw->pw_name != NULL) ? pw->pw_name : "";
}

/* escalate -- as_root/as_user: prepend sudo only when we are not already the
 * identity the command needs. `want_user` NULL means root. Returns a
 * NULL-terminated vector the caller must free (the strings are borrowed). */
static char **escalate(char *const argv[], const char *want_user) {
    size_t n = 0;
    size_t i;
    char **out;
    int need;
    size_t prefix;

    while (argv[n] != NULL) n++;
    if (want_user == NULL) {
        need = getuid() != 0;                        /* as_root */
        prefix = need ? 1 : 0;
    } else {
        need = *want_user != '\0' && strcmp(whoami(), want_user) != 0;  /* as_user */
        prefix = need ? 3 : 0;
    }

    out = (char **)malloc((n + prefix + 1) * sizeof(char *));
    if (out == NULL) osr_die_oom();
    if (need) {
        out[0] = (char *)"sudo";
        if (want_user != NULL) {
            out[1] = (char *)"-u";
            out[2] = (char *)want_user;
        }
    }
    for (i = 0; i < n; i++) out[prefix + i] = argv[i];
    out[prefix + n] = NULL;
    return out;
}

/* spawn -- fork+exec, wait, return the exit status. out_fd/err_fd, when not
 * -1, replace the child's stdout/stderr (the step window redirects both into
 * one log, exactly as `( "$@" ) >>log 2>&1` did). */
static int spawn(char *const argv[], int out_fd, int err_fd) {
    pid_t pid;
    int status;

    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid < 0) return 127;
    if (pid == 0) {
        if (out_fd >= 0) dup2(out_fd, 1);
        if (err_fd >= 0) dup2(err_fd, 2);
        execvp(argv[0], argv);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) return 127;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int osr_run(char *const argv[]) { return spawn(argv, -1, -1); }

int osr_run_root(char *const argv[]) {
    char **v = escalate(argv, NULL);
    int rc = spawn(v, -1, -1);
    free(v);
    return rc;
}

int osr_run_user(char *const argv[]) {
    char **v = escalate(argv, osr_mod_user());
    int rc = spawn(v, -1, -1);
    free(v);
    return rc;
}

int osr_have_cmd(const char *name) { return osr_path_lookup(name, NULL); }

int osr_run_capture(char *const argv[], Str *out) {
    int fds[2];
    pid_t pid;
    int status;

    if (pipe(fds) != 0) return 0;
    fflush(stdout);
    pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return 0; }
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        dup2(fds[1], 1);
        if (devnull >= 0) { dup2(devnull, 2); close(devnull); }
        close(fds[0]);
        close(fds[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    for (;;) {
        char buf[512];
        long n = (long)read(fds[0], buf, sizeof(buf));
        if (n <= 0) break;
        str_add(out, buf, (size_t)n);
    }
    close(fds[0]);
    if (waitpid(pid, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* osr_run_step -- run_step, with a real command in the middle. The paint loop
 * is lib/ui.c's, driven here the way lib/ui.sh drives it: the command's output
 * goes to a per-step log, the block repaints while it runs, and the whole
 * thing collapses to one line. Off a TTY (or under --verbose) it degrades to
 * `info <desc>` + streamed output, same §3 rule.
 */
int osr_run_step(const char *desc, char *const argv[]) {
    Str log_path;
    int painted = 0;
    int fd;
    pid_t pid;
    int rc;

    if (!osr_ui_live()) {
        osr_info(desc);
        if (spawn(argv, -1, -1) != 0) osr_die("%s failed", desc);
        return 1;
    }

    str_init(&log_path);
    str_addz(&log_path, env_str("OSR_LOG", "/tmp/os-rice.log"));
    str_addz(&log_path, ".step");
    fd = open(str_text(&log_path), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        str_free(&log_path);
        osr_info(desc);
        if (spawn(argv, -1, -1) != 0) osr_die("%s failed", desc);
        return 1;
    }

    fflush(stdout);
    pid = fork();
    if (pid < 0) {
        close(fd);
        str_free(&log_path);
        return 0;
    }
    if (pid == 0) {
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fd);

    /* spin_CHILD, not spin_pid: this pid is ours, and an exited child stays a
     * pid until it is reaped - polling kill(pid, 0) would spin forever. */
    painted = osr_ui_spin_child(pid, desc, str_text(&log_path), &rc);
    osr_ui_append_log(str_text(&log_path));
    osr_ui_result(painted, rc == 0, desc);
    if (rc != 0) {
        osr_ui_fail_tail(20, str_text(&log_path));
        str_free(&log_path);
        osr_die("%s failed", desc);
    }
    str_free(&log_path);
    return 1;
}

/* osr_run_step_root -- run_step "<desc>" as_root <cmd...>. */
int osr_run_step_root(const char *desc, char *const argv[]) {
    char **v = escalate(argv, NULL);
    int ok = osr_run_step(desc, v);
    free(v);
    return ok;
}

/* osr_step -- run_step around a function of this process. The child gets the
 * step log on stdout+stderr, the parent paints; identical to what
 * osr_run_step does for a command, minus the exec. */
int osr_step(const char *desc, int (*fn)(void *ctx), void *ctx) {
    Str log_path;
    int painted;
    int fd;
    pid_t pid;
    int rc;

    if (!osr_ui_live()) {
        osr_info(desc);
        if (!fn(ctx)) osr_die("%s failed", desc);
        return 1;
    }

    str_init(&log_path);
    str_addz(&log_path, env_str("OSR_LOG", "/tmp/os-rice.log"));
    str_addz(&log_path, ".step");
    fd = open(str_text(&log_path), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        str_free(&log_path);
        osr_info(desc);
        if (!fn(ctx)) osr_die("%s failed", desc);
        return 1;
    }

    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid < 0) {
        close(fd);
        str_free(&log_path);
        return 0;
    }
    if (pid == 0) {
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
        _exit(fn(ctx) ? 0 : 1);
    }
    close(fd);

    /* spin_CHILD, not spin_pid: this pid is ours, and an exited child stays a
     * pid until it is reaped - polling kill(pid, 0) would spin forever. */
    painted = osr_ui_spin_child(pid, desc, str_text(&log_path), &rc);
    osr_ui_append_log(str_text(&log_path));
    osr_ui_result(painted, rc == 0, desc);
    if (rc != 0) {
        osr_ui_fail_tail(20, str_text(&log_path));
        str_free(&log_path);
        osr_die("%s failed", desc);
    }
    str_free(&log_path);
    return 1;
}

/* --- packages ------------------------------------------------------------- */

/* row_rhs -- the value half of a pkgmap row, given the text just past the '=':
 * a trailing ` # comment` dropped (the space before # is required, so `a#b`
 * survives) and both ends trimmed. _pkgmap_rhs in lib/pkg.sh. */
static void row_rhs(Str *out, const char *p, size_t remain) {
    size_t i;
    size_t end = remain;
    for (i = 0; i + 1 < remain; i++) {
        if (is_space(p[i]) && p[i + 1] == '#') { end = i; break; }
    }
    while (end > 0 && is_space(p[end - 1])) end--;
    for (i = 0; i < end && is_space(p[i]); i++) { /* ltrim */ }
    str_add(out, p + i, end - i);
}

/* map_lookup -- one pkgmap row: `^[[:space:]]*<key>[[:space:]]*=`, then the
 * right-hand side per row_rhs. Same two files, same order, as _pkgmap_exact. */
static int map_lookup(Str *out, const char *map_path, const char *key) {
    char *buf;
    size_t len;
    size_t pos = 0;
    Line line;
    int found = 0;

    buf = slurp(map_path, &len);
    if (buf == NULL) return 0;
    while (!found && next_line(buf, len, &pos, &line)) {
        const char *p = line.start;
        size_t remain = line.len;
        size_t klen = strlen(key);
        while (remain > 0 && is_space(*p)) { p++; remain--; }
        if (remain <= klen || strncmp(p, key, klen) != 0) continue;
        p += klen;
        remain -= klen;
        while (remain > 0 && is_space(*p)) { p++; remain--; }
        if (remain == 0 || *p != '=') continue;
        p++;
        remain--;
        row_rhs(out, p, remain);
        found = 1;
    }
    free(buf);
    return found;
}

/* ver_cmp -- _ver_cmp in lib/pkg.sh: component-wise numeric compare, -1/0/1.
 * Missing components count as 0 (3 == 3.0.0, and 3.21 < 3.21.3), and each
 * component keeps its leading digits only, which is what makes 15-SP5,
 * 3.24_alpha and Ubuntu's 24.04 comparable at all. */
static int ver_cmp(const char *a, const char *b) {
    while (*a != '\0' || *b != '\0') {
        long x = 0;
        long y = 0;
        while (*a >= '0' && *a <= '9') x = x * 10 + (*a++ - '0');
        while (*b >= '0' && *b <= '9') y = y * 10 + (*b++ - '0');
        if (x != y) return x > y ? 1 : -1;
        while (*a != '\0' && *a != '.') a++;
        while (*b != '\0' && *b != '.') b++;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

/* ver_match -- _ver_match: does <ver> satisfy a comparison facet (`<=3.20`,
 * `<3.22`, `>=0.66`, `>13`)? Anything not starting with an operator is not a
 * range, so a plain `name@3.20` key is never mistaken for one. */
static int ver_match(const char *ver, const char *expr) {
    int op;          /* 0 '<'   1 "<="   2 '>'   3 ">=" */
    int c;

    if (expr[0] == '<') {
        op = expr[1] == '=' ? 1 : 0;
        expr += op == 1 ? 2 : 1;
    } else if (expr[0] == '>') {
        op = expr[1] == '=' ? 3 : 2;
        expr += op == 3 ? 2 : 1;
    } else {
        return 0;
    }
    if (*expr == '\0') return 0;
    c = ver_cmp(ver, expr);
    switch (op) {
        case 0:  return c < 0;
        case 1:  return c <= 0;
        case 2:  return c > 0;
        default: return c >= 0;
    }
}

/* map_lookup_range -- the first `name@<op><ver>` row in this file whose
 * comparison holds for <ver>. Ranges cannot be ordered by specificity the way
 * exact keys can, so file order is the tie-break, exactly as in _pkgmap_range. */
static int map_lookup_range(Str *out, const char *map_path, const char *name,
                            const char *ver) {
    char *buf;
    size_t len;
    size_t pos = 0;
    Line line;
    int found = 0;
    size_t nlen = strlen(name);

    buf = slurp(map_path, &len);
    if (buf == NULL) return 0;
    while (!found && next_line(buf, len, &pos, &line)) {
        const char *p = line.start;
        size_t remain = line.len;
        Str expr;

        while (remain > 0 && is_space(*p)) { p++; remain--; }
        if (remain <= nlen + 1 || strncmp(p, name, nlen) != 0 || p[nlen] != '@') continue;
        p += nlen + 1;
        remain -= nlen + 1;
        if (remain == 0 || (*p != '<' && *p != '>')) continue;
        /* The comparison facet: the operator, then the version. Stopping at the
         * first '=' the way the row separator is normally found would cut
         * `<=3.22` down to `<`, so the operator is consumed first and only
         * digits and dots after it (_pkgmap_range reads it with a sed for the
         * same reason). */
        str_init(&expr);
        str_addc(&expr, *p);
        p++;
        remain--;
        if (remain > 0 && *p == '=') { str_addc(&expr, *p); p++; remain--; }
        while (remain > 0 && ((*p >= '0' && *p <= '9') || *p == '.')) {
            str_addc(&expr, *p);
            p++;
            remain--;
        }
        while (remain > 0 && is_space(*p)) { p++; remain--; }
        if (remain > 0 && *p == '=' && ver_match(ver, str_text(&expr))) {
            row_rhs(out, p + 1, remain - 1);
            found = 1;
        }
        str_free(&expr);
    }
    free(buf);
    return found;
}

/* pkgmap_one -- the logical name resolved to real package name(s), most
 * specific facet first, in <manager>.map then any.map (§1a, _pkgmap_one):
 *
 *   name@trixie    codename      exact
 *   name@3.21.3    version_id    exact
 *   name@3.21      version_id    dotted prefix, longest first (then name@3)
 *   name@<=3.21    version_id    comparison, first matching row wins
 *   name@x86_64    arch          exact
 *   name           -             the bare row
 *
 * An unlisted name passes through unchanged (§1). */
void osr_pkgmap_resolve(Str *out, const char *name) {
    const char *codename = env_str("OSR_CODENAME", NULL);
    const char *version  = env_str("OSR_VERSION_ID", NULL);
    const char *arch     = env_str("OSR_ARCH", NULL);
    Str key;
    Str map;
    int stage;
    int j;
    int done = 0;

    str_init(&key);
    str_init(&map);

    /* The two map paths, rebuilt per probe: <manager>.map then any.map. */
    #define OSR_MAP_PATH(which) do {                                  \
        str_reset(&map);                                              \
        str_addz(&map, env_str("OSR_LIB", "lib"));                    \
        str_addz(&map, "/pkgmap/");                                   \
        if ((which) == 0) {                                           \
            str_addz(&map, osr_mod_pkg());                            \
            str_addz(&map, ".map");                                   \
        } else {                                                      \
            str_addz(&map, "any.map");                                \
        }                                                             \
    } while (0)

    /* stage 0 codename, 1 version_id, 2 version prefixes, 3 ranges, 4 arch,
     * 5 the bare name. */
    for (stage = 0; !done && stage <= 5; stage++) {
        if (stage == 0 && codename == NULL) continue;
        if ((stage == 1 || stage == 2 || stage == 3) && version == NULL) continue;
        if (stage == 4 && arch == NULL) continue;

        if (stage == 2) {
            /* dotted prefixes, longest first: 3.21.3 -> 3.21 -> 3 */
            size_t plen = strlen(version);
            for (;;) {
                size_t i = plen;
                while (i > 0 && version[i - 1] != '.') i--;
                if (i == 0) break;                   /* no dot left to drop */
                plen = i - 1;
                str_reset(&key);
                str_addz(&key, name);
                str_addc(&key, '@');
                str_add(&key, version, plen);
                for (j = 0; j < 2; j++) {
                    OSR_MAP_PATH(j);
                    if (map_lookup(out, str_text(&map), str_text(&key))) { done = 1; break; }
                }
                if (done) break;
            }
            continue;
        }

        if (stage == 3) {
            for (j = 0; j < 2; j++) {
                OSR_MAP_PATH(j);
                if (map_lookup_range(out, str_text(&map), name, version)) { done = 1; break; }
            }
            continue;
        }

        str_reset(&key);
        str_addz(&key, name);
        if (stage == 0) { str_addc(&key, '@'); str_addz(&key, codename); }
        if (stage == 1) { str_addc(&key, '@'); str_addz(&key, version); }
        if (stage == 4) { str_addc(&key, '@'); str_addz(&key, arch); }
        for (j = 0; j < 2; j++) {
            OSR_MAP_PATH(j);
            if (map_lookup(out, str_text(&map), str_text(&key))) { done = 1; break; }
        }
    }
    #undef OSR_MAP_PATH

    str_free(&key);
    str_free(&map);
    if (!done) str_addz(out, name);          /* not listed -> unchanged */
}

/* native_installed -- the per-manager probe _native_installed used. */
static int native_installed(const char *pkg) {
    const char *mgr = osr_mod_pkg();
    char *argv[6];
    int devnull_rc;

    if (strcmp(mgr, "apt") == 0) {
        argv[0] = (char *)"dpkg"; argv[1] = (char *)"-s"; argv[2] = (char *)pkg; argv[3] = NULL;
    } else if (strcmp(mgr, "dnf") == 0) {
        argv[0] = (char *)"rpm"; argv[1] = (char *)"-q"; argv[2] = (char *)pkg; argv[3] = NULL;
    } else if (strcmp(mgr, "pacman") == 0) {
        argv[0] = (char *)"pacman"; argv[1] = (char *)"-Q"; argv[2] = (char *)pkg; argv[3] = NULL;
    } else if (strcmp(mgr, "apk") == 0) {
        argv[0] = (char *)"apk"; argv[1] = (char *)"info"; argv[2] = (char *)"-e";
        argv[3] = (char *)pkg; argv[4] = NULL;
    } else if (strcmp(mgr, "xbps") == 0) {
        argv[0] = (char *)"xbps-query"; argv[1] = (char *)pkg; argv[2] = NULL;
    } else if (strcmp(mgr, "portage") == 0) {
        if (osr_have_cmd("qlist")) {
            argv[0] = (char *)"qlist"; argv[1] = (char *)"-I"; argv[2] = (char *)"-e";
            argv[3] = (char *)pkg; argv[4] = NULL;
        } else {
            argv[0] = (char *)"portageq"; argv[1] = (char *)"has_version";
            argv[2] = (char *)"/"; argv[3] = (char *)pkg; argv[4] = NULL;
        }
    } else {
        return 0;
    }
    devnull_rc = osr_run_quiet(argv);
    return devnull_rc == 0;
}

int osr_pkg_installed(const char *name) {
    Str rhs;
    int ok = 1;
    const char *p;

    str_init(&rhs);
    osr_pkgmap_resolve(&rhs, name);
    p = str_text(&rhs);
    if (strncmp(p, "aur:", 4) == 0) {
        char *argv[4];
        argv[0] = (char *)"pacman"; argv[1] = (char *)"-Q";
        argv[2] = (char *)(p + 4); argv[3] = NULL;
        ok = osr_run_quiet(argv) == 0;
    } else if (strncmp(p, "script:", 7) == 0 || strncmp(p, "source:", 7) == 0 ||
               strncmp(p, "cargo:", 6) == 0) {
        ok = osr_have_cmd(name);
    } else {
        Str word;
        str_init(&word);
        while (*p != '\0' && ok) {
            while (is_space(*p)) p++;
            str_reset(&word);
            while (*p != '\0' && !is_space(*p)) str_addc(&word, *p++);
            if (word.len > 0) ok = native_installed(str_text(&word));
        }
        str_free(&word);
    }
    str_free(&rhs);
    return ok;
}

/* pkg_refresh -- once per process, lazily, right before the first install:
 * a fresh container has no package lists yet. */
static int refreshed = 0;

static void pkg_refresh(void) {
    const char *mgr = osr_mod_pkg();
    char *argv[8];

    if (refreshed) return;
    refreshed = 1;
    if (strcmp(mgr, "apt") == 0) {
        argv[0] = (char *)"env"; argv[1] = (char *)"DEBIAN_FRONTEND=noninteractive";
        argv[2] = (char *)"apt-get"; argv[3] = (char *)"update"; argv[4] = (char *)"-q";
        argv[5] = (char *)"-o"; argv[6] = (char *)"Dpkg::Use-Pty=0";
        argv[7] = NULL;
    } else if (strcmp(mgr, "dnf") == 0) {
        argv[0] = (char *)"dnf"; argv[1] = (char *)"-q"; argv[2] = (char *)"makecache"; argv[3] = NULL;
    } else if (strcmp(mgr, "pacman") == 0) {
        argv[0] = (char *)"pacman"; argv[1] = (char *)"-Sy"; argv[2] = (char *)"--noconfirm"; argv[3] = NULL;
    } else if (strcmp(mgr, "apk") == 0) {
        argv[0] = (char *)"apk"; argv[1] = (char *)"update"; argv[2] = NULL;
    } else if (strcmp(mgr, "xbps") == 0) {
        argv[0] = (char *)"xbps-install"; argv[1] = (char *)"-S"; argv[2] = NULL;
    } else if (strcmp(mgr, "portage") == 0) {
        argv[0] = (char *)"emerge"; argv[1] = (char *)"--sync"; argv[2] = (char *)"--quiet"; argv[3] = NULL;
    } else {
        return;
    }
    if (osr_run_root(argv) != 0) osr_warn("package index refresh failed - continuing");
}

/* pkg_install_via_sh -- run lib/pkg.sh's pkg_install for one package, with the
 * libs it needs sourced around it. The facts are already exported, so
 * detect.sh only defines functions here; nothing is re-detected. */
static int pkg_install_via_sh(const char *name) {
    Str script;
    char *argv[6];
    int rc;

    str_init(&script);
    str_addz(&script, ". \"$OSR_LIB/ui.sh\"; . \"$OSR_LIB/log.sh\"; ");
    str_addz(&script, "for l in detect user net pkg git config build; do ");
    str_addz(&script, "[ -f \"$OSR_LIB/$l.sh\" ] && . \"$OSR_LIB/$l.sh\"; done; ");
    str_addz(&script, "pkg_install \"$1\"");
    argv[0] = (char *)"sh";
    argv[1] = (char *)"-c";
    argv[2] = script.p;
    argv[3] = (char *)"_";
    argv[4] = (char *)name;
    argv[5] = NULL;
    rc = osr_run(argv);
    str_free(&script);
    if (rc != 0) osr_warnf("provider install failed for %s (exit %d)", name, rc);
    return rc == 0;
}

int osr_pkg_install(const char *const names[]) {
    Str todo;                 /* the real package names still to install */
    Str desc;
    char **argv;
    size_t argc = 0;
    size_t i;
    const char *mgr = osr_mod_pkg();
    int rc;

    str_init(&todo);
    for (i = 0; names[i] != NULL; i++) {
        Str rhs;
        const char *p;
        str_init(&rhs);
        osr_pkgmap_resolve(&rhs, names[i]);
        p = str_text(&rhs);
        if (strncmp(p, "aur:", 4) == 0 || strncmp(p, "script:", 7) == 0 ||
            strncmp(p, "source:", 7) == 0 || strncmp(p, "cargo:", 6) == 0) {
            /* A provider row (build from source, run an install script, AUR,
             * cargo). Those live in lib/pkg.sh and its provide/ builders, and
             * porting the download/build stack is a separate job -- so hand
             * this one package back to the shell tier that owns it, rather
             * than refusing to install or guessing a native name.
             *
             * This is the one place the C tier calls back into sh, and it is
             * temporary by construction: when the providers are ported, this
             * branch goes away and nothing else changes. */
            str_free(&rhs);
            if (!pkg_install_via_sh(names[i])) {
                str_free(&todo);
                return 0;
            }
            continue;
        }
        while (*p != '\0') {
            Str word;
            str_init(&word);
            while (is_space(*p)) p++;
            while (*p != '\0' && !is_space(*p)) str_addc(&word, *p++);
            if (word.len > 0) {
                if (native_installed(str_text(&word))) {
                    osr_infof("%s already installed - skipping", str_text(&word));
                } else {
                    if (todo.len > 0) str_addc(&todo, ' ');
                    str_add(&todo, str_text(&word), word.len);
                }
            }
            str_free(&word);
        }
        str_free(&rhs);
    }
    if (todo.len == 0) { str_free(&todo); return 1; }

    pkg_refresh();

    /* one install command for everything left */
    argv = (char **)calloc(16 + todo.len, sizeof(char *));
    if (argv == NULL) osr_die_oom();
    if (strcmp(mgr, "apt") == 0) {
        argv[argc++] = (char *)"env";
        argv[argc++] = (char *)"DEBIAN_FRONTEND=noninteractive";
        argv[argc++] = (char *)"apt-get";
        argv[argc++] = (char *)"install";
        argv[argc++] = (char *)"-y";
        /* -q and no dpkg pty: the step log is a file, and apt/dpkg's
         * in-place progress redraws only make the tail window churn. */
        argv[argc++] = (char *)"-q";
        argv[argc++] = (char *)"-o";
        argv[argc++] = (char *)"Dpkg::Use-Pty=0";
    } else if (strcmp(mgr, "dnf") == 0) {
        argv[argc++] = (char *)"dnf"; argv[argc++] = (char *)"install"; argv[argc++] = (char *)"-y";
    } else if (strcmp(mgr, "pacman") == 0) {
        argv[argc++] = (char *)"pacman"; argv[argc++] = (char *)"-S";
        argv[argc++] = (char *)"--needed"; argv[argc++] = (char *)"--noconfirm";
    } else if (strcmp(mgr, "apk") == 0) {
        argv[argc++] = (char *)"apk"; argv[argc++] = (char *)"add";
    } else if (strcmp(mgr, "xbps") == 0) {
        argv[argc++] = (char *)"xbps-install"; argv[argc++] = (char *)"-y";
    } else if (strcmp(mgr, "portage") == 0) {
        argv[argc++] = (char *)"emerge"; argv[argc++] = (char *)"--quiet";
        argv[argc++] = (char *)"--noreplace"; argv[argc++] = (char *)"--getbinpkg";
    } else {
        osr_warnf("no native installer for OSR_PKG='%s'", mgr);
        free(argv);
        str_free(&todo);
        return 0;
    }
    {
        char *p = todo.p;
        while (*p != '\0') {
            while (*p == ' ') *p++ = '\0';
            if (*p == '\0') break;
            argv[argc++] = p;
            while (*p != '\0' && *p != ' ') p++;
        }
    }
    argv[argc] = NULL;

    str_init(&desc);
    rc = osr_run_root(argv);
    if (rc != 0) osr_warnf("native install failed (exit %d)", rc);
    str_free(&desc);
    free(argv);
    str_free(&todo);
    return rc == 0;
}

static int pkg_install_thunk(void *ctx) {
    return osr_pkg_install((const char *const *)ctx);
}

int osr_pkg_install_step(const char *desc, const char *const names[]) {
    return osr_step(desc, pkg_install_thunk, (void *)names);
}

/* --- services ------------------------------------------------------------- */

/* service_resolve -- lib/servicemap: `logical <init> real`, only where they
 * differ; an unlisted name is its own unit. */
static void service_resolve(Str *out, const char *name) {
    Str path;
    char *buf;
    size_t len;
    size_t pos = 0;
    Line line;
    int found = 0;

    str_init(&path);
    str_addz(&path, env_str("OSR_LIB", "lib"));
    str_addz(&path, "/servicemap");
    buf = slurp(str_text(&path), &len);
    str_free(&path);
    if (buf != NULL) {
        while (!found && next_line(buf, len, &pos, &line)) {
            Str l;
            const char *p;
            Str f1;
            Str f2;
            Str f3;
            str_init(&l);
            str_add(&l, line.start, line.len);
            p = str_text(&l);
            str_init(&f1); str_init(&f2); str_init(&f3);
            while (is_space(*p)) p++;
            while (*p != '\0' && !is_space(*p)) str_addc(&f1, *p++);
            while (is_space(*p)) p++;
            while (*p != '\0' && !is_space(*p)) str_addc(&f2, *p++);
            while (is_space(*p)) p++;
            while (*p != '\0' && !is_space(*p)) str_addc(&f3, *p++);
            if (str_text(&f1)[0] != '#' && strcmp(str_text(&f1), name) == 0 &&
                strcmp(str_text(&f2), osr_mod_init()) == 0 && f3.len > 0) {
                str_addz(out, str_text(&f3));
                found = 1;
            }
            str_free(&f1); str_free(&f2); str_free(&f3);
            str_free(&l);
        }
        free(buf);
    }
    if (!found) str_addz(out, name);
}

int osr_service_enable(const char *name) {
    Str svc;
    const char *init = osr_mod_init();
    char *argv[6];
    int rc = 1;

    str_init(&svc);
    service_resolve(&svc, name);

    if (strcmp(init, "systemd") == 0) {
        char *chk[4];
        chk[0] = (char *)"systemctl"; chk[1] = (char *)"is-enabled";
        chk[2] = svc.p; chk[3] = NULL;
        if (osr_run_quiet(chk) == 0) {
            chk[1] = (char *)"is-active";
            if (osr_run_quiet(chk) == 0) {
                osr_infof("%s already enabled + running - skipping", str_text(&svc));
                str_free(&svc);
                return 1;
            }
        }
        argv[0] = (char *)"systemctl"; argv[1] = (char *)"enable";
        argv[2] = (char *)"--now"; argv[3] = svc.p; argv[4] = NULL;
        rc = osr_run_root(argv) == 0;
    } else if (strcmp(init, "openrc") == 0) {
        argv[0] = (char *)"rc-update"; argv[1] = (char *)"add";
        argv[2] = svc.p; argv[3] = (char *)"default"; argv[4] = NULL;
        rc = osr_run_root(argv) == 0;
        argv[0] = (char *)"rc-service"; argv[1] = svc.p; argv[2] = (char *)"start"; argv[3] = NULL;
        rc = (osr_run_root(argv) == 0) && rc;
    } else if (strcmp(init, "runit") == 0) {
        Str sv;
        Str run;
        str_init(&sv);
        str_addz(&sv, env_str("OSR_SV_DIR", "/etc/sv"));
        str_addc(&sv, '/');
        str_addz(&sv, str_text(&svc));
        str_init(&run);
        str_addz(&run, env_str("OSR_SERVICE_DIR", "/var/service"));
        str_addc(&run, '/');
        str_addz(&run, str_text(&svc));
        /* ln -s succeeds even when the target is missing, so an unpackaged
         * service would leave a dangling link runsvdir complains about
         * forever. Check first and degrade to a warning. */
        if (!dir_exists(str_text(&sv))) {
            osr_warnf("no %s - skipping (package ships no runit service)", str_text(&sv));
        } else if (!file_exists(str_text(&run)) && !dir_exists(str_text(&run))) {
            argv[0] = (char *)"ln"; argv[1] = (char *)"-s"; argv[2] = sv.p;
            argv[3] = run.p; argv[4] = NULL;
            rc = osr_run_root(argv) == 0;
        }
        str_free(&sv);
        str_free(&run);
    } else if (strcmp(init, "sysvinit") == 0) {
        argv[0] = (char *)"update-rc.d"; argv[1] = svc.p; argv[2] = (char *)"enable"; argv[3] = NULL;
        rc = osr_run_root(argv) == 0;
        argv[0] = (char *)"service"; argv[1] = svc.p; argv[2] = (char *)"start"; argv[3] = NULL;
        rc = (osr_run_root(argv) == 0) && rc;
    } else {
        osr_warnf("enable_service: unknown init '%s' - skipping %s", init, str_text(&svc));
    }
    str_free(&svc);
    return rc;
}

/* --- files ---------------------------------------------------------------- */

int osr_mkdir_p(const char *dir) {
    char *argv[4];
    argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p"; argv[2] = (char *)dir; argv[3] = NULL;
    return osr_run_user(argv) == 0;
}

/* dir_of -- `dirname`. */
static void dir_of(Str *out, const char *path) {
    const char *slash = strrchr(path, '/');
    if (slash == NULL) { str_addc(out, '.'); return; }
    if (slash == path) { str_addc(out, '/'); return; }
    str_add(out, path, (size_t)(slash - path));
}

int osr_install_file(const char *src, const char *dst) {
    Str dir;
    char *argv[5];
    int ok;

    if (!file_exists(src)) {
        osr_warnf("install: source not found: %s", src);
        return 0;
    }
    if (file_exists(dst) && osr_files_equal(src, dst)) return 1;   /* §2: nothing to do */
    if (file_exists(dst)) {
        Str bak;
        str_init(&bak);
        str_addz(&bak, dst);
        str_addz(&bak, ".bak");
        if (!file_exists(str_text(&bak))) {
            argv[0] = (char *)"cp"; argv[1] = (char *)"-f"; argv[2] = (char *)dst;
            argv[3] = bak.p; argv[4] = NULL;
            osr_run_user(argv);
        }
        str_free(&bak);
    }
    str_init(&dir);
    dir_of(&dir, dst);
    osr_mkdir_p(str_text(&dir));
    str_free(&dir);
    argv[0] = (char *)"cp"; argv[1] = (char *)"-f"; argv[2] = (char *)src;
    argv[3] = (char *)dst; argv[4] = NULL;
    ok = osr_run_user(argv) == 0;
    return ok;
}

/* append_as_user -- `printf '%s\n' "$line" | as_user tee -a "$file"`: the
 * append has to happen as the riced user, and a plain fopen here would create
 * a root-owned file in their home. */
static int append_as_user(const char *file, const char *line) {
    char *argv[5];
    char **v;
    int fds[2];
    pid_t pid;
    int status;

    argv[0] = (char *)"tee";
    argv[1] = (char *)"-a";
    argv[2] = (char *)file;
    argv[3] = NULL;
    v = escalate(argv, osr_mod_user());

    if (pipe(fds) != 0) { free(v); return 0; }
    fflush(stdout);
    pid = fork();
    if (pid < 0) { free(v); return 0; }
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        dup2(fds[0], 0);
        close(fds[0]);
        close(fds[1]);
        if (devnull >= 0) { dup2(devnull, 1); close(devnull); }
        execvp(v[0], v);
        _exit(127);
    }
    close(fds[0]);
    {
        size_t len = strlen(line);
        if (len > 0) { if (write(fds[1], line, len) < 0) { /* reported below */ } }
        if (write(fds[1], "\n", 1) < 0) { /* reported below */ }
    }
    close(fds[1]);
    free(v);
    if (waitpid(pid, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int osr_install_layer(const char *src, const char *dst) {
    return osr_install_file(src, dst);
}

int osr_install_theme_layer(const char *app, const char *name, const char *dst) {
    Str src;
    int is_temp = 0;
    int ok;

    str_init(&src);
    if (!osr_theme_source(&src, app, name, &is_temp)) {
        str_free(&src);
        return 0;
    }
    ok = osr_install_layer(str_text(&src), dst);
    if (is_temp) remove(str_text(&src));
    str_free(&src);
    return ok;
}

int osr_ensure_line(const char *file, const char *line) {
    Str dir;
    char *buf;
    size_t len;
    int present = 0;

    str_init(&dir);
    dir_of(&dir, file);
    osr_mkdir_p(str_text(&dir));
    str_free(&dir);

    buf = slurp(file, &len);
    if (buf != NULL) {
        size_t nlen = strlen(line);
        size_t i;
        for (i = 0; i + nlen <= len && !present; i++) {
            if (memcmp(buf + i, line, nlen) == 0) present = 1;   /* grep -F */
        }
        free(buf);
    }
    if (present) return 1;
    return append_as_user(file, line);
}
