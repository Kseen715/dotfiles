/* lib/module.c -- the implementation of lib/module.h, the API a Linux module
 * written in C is allowed to use.
 *
 * Everything here is the C form of something a .sh module called: run_step,
 * pkg_install, enable_service, as_root/as_user, backup_copy, ensure_line. The
 * sh versions stay where they are (lib/pkg.sh, lib/service.sh, lib/config.sh)
 * because ~118 shell modules still call them; these are the same behaviors
 * for the modules that have moved to C.
 *
 * Packages are no longer here: they moved to lib/pkg.c when they grew the
 * provider methods (script:, cargo:, aur:), which is a whole unit's worth.
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
static int spawn_io(char *const argv[], int in_fd, int out_fd, int err_fd) {
    pid_t pid;
    int status;

    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid < 0) return 127;
    if (pid == 0) {
        if (in_fd >= 0) dup2(in_fd, 0);
        if (out_fd >= 0) dup2(out_fd, 1);
        if (err_fd >= 0) dup2(err_fd, 2);
        execvp(argv[0], argv);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) return 127;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static int spawn(char *const argv[], int out_fd, int err_fd) {
    return spawn_io(argv, -1, out_fd, err_fd);
}

int osr_run(char *const argv[]) { return spawn(argv, -1, -1); }

int osr_run_root(char *const argv[]) {
    char **v = escalate(argv, NULL);
    int rc = spawn(v, -1, -1);
    free(v);
    return rc;
}

/* osr_run_root_quiet -- as_root with both streams on /dev/null, the
 * `as_root <cmd> >/dev/null 2>&1 || :` shape of a best-effort probe. */
int osr_run_root_quiet(char *const argv[]) {
    char **v = escalate(argv, NULL);
    int fd = open("/dev/null", O_WRONLY);
    int rc = spawn(v, fd, fd);
    if (fd >= 0) close(fd);
    free(v);
    return rc;
}

/* osr_run_user_quiet -- as_user with both streams on /dev/null, the
 * `as_user <cmd> >/dev/null 2>&1 || warn ...` shape of a best-effort action
 * (handing an image to a wallpaper setter that may not be running). */
int osr_run_user_quiet(char *const argv[]) {
    char **v = escalate(argv, osr_mod_user());
    int fd = open("/dev/null", O_WRONLY);
    int rc = spawn(v, fd, fd);
    if (fd >= 0) close(fd);
    free(v);
    return rc;
}

int osr_run_user(char *const argv[]) {
    char **v = escalate(argv, osr_mod_user());
    int rc = spawn(v, -1, -1);
    free(v);
    return rc;
}

/* osr_run_user_in -- as_user with the child's stdin replaced, which is the C
 * form of the one shape a pipeline needs: `<fetch> | as_user sh -s -- args`,
 * lib/pkg.sh's script: provider. */
int osr_run_user_in(char *const argv[], int in_fd) {
    char **v = escalate(argv, osr_mod_user());
    int rc = spawn_io(v, in_fd, -1, -1);
    free(v);
    return rc;
}

int osr_have_cmd(const char *name) { return osr_path_lookup(name, NULL); }

/* capture -- the shared body of the two capture helpers: run argv, collect its
 * stdout, and either discard stderr or fold it into the same pipe. */
static int capture(char *const argv[], Str *out, int merge_err) {
    int fds[2];
    pid_t pid;
    int status;

    if (pipe(fds) != 0) return 0;
    fflush(stdout);
    pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return 0; }
    if (pid == 0) {
        dup2(fds[1], 1);
        if (merge_err) {
            dup2(fds[1], 2);
        } else {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) { dup2(devnull, 2); close(devnull); }
        }
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

int osr_run_capture(char *const argv[], Str *out) { return capture(argv, out, 0); }

/* osr_run_capture_err -- `<cmd> 2>&1`, for a tool that reports on stderr and
 * whose report IS the answer (wget --spider prints headers there). */
int osr_run_capture_err(char *const argv[], Str *out) { return capture(argv, out, 1); }

/* osr_run_root_capture -- `as_root <cmd> 2>&1`: a privileged probe whose
 * DIAGNOSTICS are the answer, so stderr belongs in the captured text. */
int osr_run_root_capture(char *const argv[], Str *out) {
    char **v = escalate(argv, NULL);
    int ok = capture(v, out, 1);
    free(v);
    return ok;
}

/* osr_run_user_capture -- `as_user <cmd> 2>/dev/null`: a probe the riced
 * account has to make itself, because the answer depends on that account
 * (git's view of a repo it owns). Its stderr is noise, not the answer. */
int osr_run_user_capture(char *const argv[], Str *out) {
    char **v = escalate(argv, osr_mod_user());
    int ok = capture(v, out, 0);
    free(v);
    return ok;
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

/* tee_write -- `printf '%s' "$text" | as_<who> tee [-a] "$file"`, which is the
 * C form of every heredoc a .sh module piped into tee. It has to be tee and not
 * fopen for the same reason the shell used tee: the write must happen AS the
 * target identity, or a module run under sudo leaves a root-owned file in the
 * riced user's home (and a user-owned one under /usr, which is worse).
 *
 * `as_root` selects the identity, `append` selects tee -a. A NULL trailer means
 * "text is already exactly what should be on disk"; otherwise trailer is written
 * after it, which is how the one-line append keeps its newline. */
static int tee_write(const char *file, const char *text, const char *trailer,
                     int as_root, int append) {
    char *argv[5];
    char **v;
    int fds[2];
    pid_t pid;
    int status;
    int i = 0;

    argv[i++] = (char *)"tee";
    if (append) argv[i++] = (char *)"-a";
    argv[i++] = (char *)file;
    argv[i] = NULL;
    v = escalate(argv, as_root ? NULL : osr_mod_user());

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
        size_t len = strlen(text);
        if (len > 0) { if (write(fds[1], text, len) < 0) { /* reported below */ } }
        if (trailer != NULL && *trailer != '\0') {
            if (write(fds[1], trailer, strlen(trailer)) < 0) { /* reported below */ }
        }
    }
    close(fds[1]);
    free(v);
    if (waitpid(pid, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* append_as_user -- one line into a file owned by the riced user. */
static int append_as_user(const char *file, const char *line) {
    return tee_write(file, line, "\n", 0, 1);
}

/* seed -- the `[ -f x ] || as_<who> tee x <<EOF` shape, shared by the two
 * public entry points below. Already-there is success, not a no-op to report:
 * §5 says a seeded file becomes the machine's the moment it exists, so a rerun
 * must not overwrite what the user edited. */
static int seed(const char *dst, const char *content, int as_root) {
    Str dir;

    if (file_exists(dst)) return 1;

    str_init(&dir);
    dir_of(&dir, dst);
    if (as_root) {
        char *argv[4];
        argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p"; argv[2] = dir.p; argv[3] = NULL;
        osr_run_root(argv);
    } else {
        osr_mkdir_p(str_text(&dir));
    }
    str_free(&dir);

    osr_infof("seeding %s", dst);
    return tee_write(dst, content, NULL, as_root, 0);
}

int osr_seed_file(const char *dst, const char *content) {
    return seed(dst, content, 0);
}

int osr_seed_file_root(const char *dst, const char *content) {
    return seed(dst, content, 1);
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
