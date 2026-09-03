/* lib/module.c -- the implementation of lib/module.h, the API a module written
 * in C is allowed to use, on either operating system.
 *
 * ONE FILE, TWO BODIES. Everything here is the C form of something a .sh
 * module called -- run_step, enable_service, as_root/as_user, backup_copy,
 * ensure_line -- and the two systems answer several of those differently
 * enough that they are written out separately below rather than threaded
 * through with #ifdefs line by line. What they are NOT is two headers or two
 * module trees: a module is modules/<name>.c exporting osrm_<name>(void), and
 * which of the two bodies below it links against is nob.c's decision, not
 * the module's.
 *
 * Read the POSIX body for the design; the Windows one is written against it,
 * and its header comment lists the four places the two genuinely diverge
 * (privilege is per process rather than per command; there is no fork; a
 * command is a line rather than a vector; the package half lives in
 * lib/pkg.c on both sides).
 *
 * Packages are not here on either side: they moved to lib/pkg.c when they
 * grew the provider methods (script:, cargo:, aur:, source: -- and, on
 * Windows, scoop:/choco:/winget:), which is a whole unit's worth.
 *
 * C89 + POSIX, and C89 + Win32.
 */
#ifndef _WIN32

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

/* --- saying things --------------------------------------------------------
 * osr_infof and its four siblings are lib/common.c's now: both cores print
 * through them, so they belong with the line shape they use rather than in
 * the POSIX module runtime. See lib/common.h.
 * ------------------------------------------------------------------------- */

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

/* --- theme-only mode ------------------------------------------------------ */

static int theme_only = -1;

int osr_theme_only(void) {
    if (theme_only < 0) theme_only = *env_str("OSR_THEME_ONLY", "") != '\0';
    return theme_only;
}

void osr_set_theme_only(int on) { theme_only = on ? 1 : 0; }

int osr_theme_only_skip(const char *verb) {
    osr_debugf("theme-apply: skipped %s", verb);
    return 1;
}

/* can_root -- a theme apply escalates only with a ticket already in hand: a
 * hotkey has no terminal to type a password into, and a blocked sudo prompt
 * would hang the switch forever. Asked once. (A few theme layers are genuinely
 * root-owned -- the LightDM greeter's conf lives in /etc -- so this is a
 * question, not a blanket no.) */
static int can_root(void) {
    static int answer = -1;
    char *argv[4];

    if (answer >= 0) return answer;
    if (getuid() == 0) { answer = 1; return answer; }
    argv[0] = (char *)"sudo"; argv[1] = (char *)"-n"; argv[2] = (char *)"true";
    argv[3] = NULL;
    answer = osr_run_quiet(argv) == 0;
    return answer;
}

int osr_run(char *const argv[]) { return spawn(argv, -1, -1); }

int osr_run_root(char *const argv[]) {
    char **v;
    int rc;

    if (osr_theme_only() && !can_root()) {
        osr_debugf("theme-apply: no sudo ticket - skipped root step: %s", argv[0]);
        return 0;
    }
    v = escalate(argv, NULL);
    rc = spawn(v, -1, -1);
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

/* osr_run_user_quiet_in -- as_user with stdin replaced and stdout discarded,
 * the `<text> | as_user tee -a "$file" >/dev/null` shape lib/migrate.sh uses to
 * append to a user-owned file. stderr is left alone so a real write failure is
 * still seen. */
int osr_run_user_quiet_in(char *const argv[], int in_fd) {
    char **v = escalate(argv, osr_mod_user());
    int fd = open("/dev/null", O_WRONLY);
    int rc = spawn_io(v, in_fd, fd, -1);
    if (fd >= 0) close(fd);
    free(v);
    return rc;
}

/* osr_run_root_in -- as_root with the child's stdin replaced: the other half of
 * that pipeline shape, `<fetch> | as_root bash`, where the installer script has
 * to run privileged rather than as the riced account. */
int osr_run_root_in(char *const argv[], int in_fd) {
    char **v = escalate(argv, NULL);
    int rc = spawn_io(v, in_fd, -1, -1);
    free(v);
    return rc;
}

/* osr_run_root_quiet_in -- as_root with stdin replaced and stdout discarded,
 * the `<text> | as_root tee "$file" >/dev/null` shape a builder uses to write a
 * .desktop entry or an apt source list. stderr is left alone so a real write
 * failure is still seen. */
int osr_run_root_quiet_in(char *const argv[], int in_fd) {
    char **v = escalate(argv, NULL);
    int fd = open("/dev/null", O_WRONLY);
    int rc = spawn_io(v, in_fd, fd, -1);
    if (fd >= 0) close(fd);
    free(v);
    return rc;
}

int osr_have_cmd(const char *name) { return osr_path_lookup(name, NULL); }

/* osr_initramfs_regen -- see lib/module.h.
 *
 * One generator per distro family and no overlap in practice: dracut (Void,
 * Fedora, RHEL), mkinitcpio (Arch), update-initramfs (Debian/Ubuntu). dracut is
 * tried first because a box that has both dracut and update-initramfs (Debian
 * with dracut installed) boots the dracut image. `-P`/`-k all` rebuild EVERY
 * installed kernel, not just the running one: the blacklist or the microcode
 * has to be in the image the next boot picks, which after a kernel upgrade is
 * not the one running now. */
int osr_initramfs_regen(void) {
    char *argv[5];

    if (osr_theme_only()) return osr_theme_only_skip("initramfs regeneration");

    if (osr_have_cmd("dracut")) {
        osr_info("Rebuilding the initramfs (dracut)");
        argv[0] = (char *)"dracut"; argv[1] = (char *)"--force"; argv[2] = NULL;
        return osr_run_root(argv) == 0;
    }
    if (osr_have_cmd("mkinitcpio")) {
        osr_info("Rebuilding the initramfs (mkinitcpio)");
        argv[0] = (char *)"mkinitcpio"; argv[1] = (char *)"-P"; argv[2] = NULL;
        return osr_run_root(argv) == 0;
    }
    if (osr_have_cmd("update-initramfs")) {
        osr_info("Rebuilding the initramfs (update-initramfs)");
        argv[0] = (char *)"update-initramfs"; argv[1] = (char *)"-u";
        argv[2] = (char *)"-k"; argv[3] = (char *)"all"; argv[4] = NULL;
        return osr_run_root(argv) == 0;
    }
    osr_warn("no initramfs generator found (dracut/mkinitcpio/update-initramfs) - "
             "skipping the rebuild");
    return 1;
}

/* persist_cap -- keep a file capability across package upgrades.
 *
 * dpkg and pacman REPLACE a program's file rather than editing it, and the
 * replacement carries no xattrs -- so the capability is gone after the next
 * upgrade of the package that owns it, silently: btop simply stops showing the
 * GPU again. Holding the package would also fix that, by never upgrading it,
 * at the price of never getting a fix for it either -- and a hold is user state
 * lib/pkg.c deliberately refuses to create or override (G2), so os-rice putting
 * one in would be the installer overruling the user with its own policy. The
 * capability is what needs to survive, not the version; a hook reapplies it.
 *
 * apt and pacman are the two managers with a drop-in for this. dnf needs a
 * Python plugin and xbps/apk/portage have no post-transaction hook at all, so
 * there the loss is reported and a module rerun is the fix. */
static void persist_cap(const char *caps, const char *setcap, const char *path) {
    const char *mgr = osr_mod_pkg();
    const char *base = strrchr(path, '/');
    Str file, body;

    base = (base != NULL) ? base + 1 : path;
    str_init(&file); str_init(&body);

    if (strcmp(mgr, "apt") == 0) {
        str_addz(&file, env_str("OSR_APT_CONF_DIR", "/etc/apt/apt.conf.d"));
        str_addz(&file, "/99-osr-setcap-");
        str_addz(&file, base);
        str_addz(&body, "// managed by os-rice: dpkg drops file capabilities on\n"
                        "// every unpack, and the program stops working as itself\n"
                        "// with no error anywhere. Reapplied after each dpkg run.\n"
                        "DPkg::Post-Invoke { \"test -x ");
        str_addz(&body, path);
        str_addz(&body, " && ");
        str_addz(&body, setcap); str_addc(&body, ' '); str_addz(&body, caps);
        str_addc(&body, ' '); str_addz(&body, path);
        str_addz(&body, " || true\"; };\n");
    } else if (strcmp(mgr, "pacman") == 0) {
        const char *dir = env_str("OSR_PACMAN_HOOK_DIR", "/etc/pacman.d/hooks");
        char *argv[4];
        argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
        argv[2] = (char *)dir; argv[3] = NULL;
        (void)osr_run_root_quiet(argv);
        str_addz(&file, dir); str_addz(&file, "/99-osr-setcap-");
        str_addz(&file, base); str_addz(&file, ".hook");
        /* Type = Path, not Package: the target is the FILE whose capability is
         * being restored, so the hook needs no knowledge of which package ships
         * it (intel_gpu_top comes from intel-gpu-tools, btop from btop). */
        str_addz(&body, "# managed by os-rice: pacman replaces the file on upgrade\n"
                        "# and the replacement carries no capabilities.\n"
                        "[Trigger]\nOperation = Install\nOperation = Upgrade\n"
                        "Type = Path\nTarget = ");
        str_addz(&body, path[0] == '/' ? path + 1 : path);
        str_addz(&body, "\n\n[Action]\nDescription = Restoring ");
        str_addz(&body, caps); str_addz(&body, " on "); str_addz(&body, base);
        str_addz(&body, "\nWhen = PostTransaction\nExec = ");
        str_addz(&body, setcap); str_addc(&body, ' '); str_addz(&body, caps);
        str_addc(&body, ' '); str_addz(&body, path); str_addc(&body, '\n');
    } else {
        osr_warnf("%s has no package hook here - %s loses %s on its next upgrade, "
                  "rerun the module to restore it", mgr, base, caps);
    }

    if (file.len > 0 && !osr_write_root(str_text(&file), str_text(&body)))
        osr_warnf("could not write %s - %s loses %s on its next upgrade",
                  str_text(&file), base, caps);
    str_free(&file); str_free(&body);
}

/* osr_setcap -- see module.h. setcap itself lives in libcap (libcap2-bin on
 * Debian), which is not a hard dependency of anything here, so a box without it
 * is warned about rather than failed: the capability is an optimisation of
 * permissions, never the thing being installed. */
int osr_setcap(const char *caps, const char *cmd) {
    Str path, setcap;
    char *argv[4];
    int ok = 0;

    if (!osr_have_cmd(cmd)) return 0;
    str_init(&path); str_init(&setcap);
    if (!osr_path_lookup("setcap", &setcap)) {
        osr_warnf("setcap is missing - %s will not get %s", cmd, caps);
    } else if (osr_path_lookup(cmd, &path)) {
        argv[0] = (char *)str_text(&setcap);
        argv[1] = (char *)caps;
        argv[2] = (char *)str_text(&path);
        argv[3] = NULL;
        ok = osr_run_root_quiet(argv) == 0;
        if (ok) {
            osr_infof("%s granted %s", str_text(&path), caps);
            persist_cap(caps, str_text(&setcap), str_text(&path));
        } else {
            osr_warnf("could not grant %s to %s", caps, str_text(&path));
        }
    }
    str_free(&path); str_free(&setcap);
    return ok;
}

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

/* detach_stdin -- a step whose output is CAPTURED must not keep the terminal on
 * stdin. The step child's stdout and stderr are the step log, so it can never
 * be interactive; what stdin still buys it is a way to hang. apt is the case
 * that proved it: `sudo` on Ubuntu 26.04 is sudo-rs, which runs the command in
 * a session and pty of its own, and apt's terminal handoff to dpkg across that
 * boundary lands the pre-configure fork in state T (stopped, no tracer) with
 * apt waiting on it forever -- an install that never finishes and never fails.
 * With stdin on /dev/null there is no terminal to hand over and the same
 * install completes. `-y`, DEBIAN_FRONTEND=noninteractive and Dpkg::Use-Pty=0
 * do NOT cover this; they answer questions, and nothing was being asked.
 *
 * Only on the captured path: off a TTY / under --verbose (osr_ui_live() == 0)
 * output is streamed and a step may legitimately be interactive -- a source
 * build's sudo password prompt is read there. sudo itself reads /dev/tty
 * rather than stdin, so its prompt still works on this path too.
 */
static void detach_stdin(void) {
    int null_fd = open("/dev/null", O_RDONLY);
    if (null_fd >= 0) {
        dup2(null_fd, 0);
        if (null_fd > 2) close(null_fd);
    }
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
        detach_stdin();
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

/* osr_run_step_user -- the same around an as_user command, the
 * `run_step "..." as_user <cmd>` a module uses when the thing being run has to
 * be the riced account (an AUR helper: makepkg refuses root). */
int osr_run_step_user(const char *desc, char *const argv[]) {
    char **v = escalate(argv, osr_mod_user());
    int ok = osr_run_step(desc, v);
    free(v);
    return ok;
}

/* osr_step -- run_step around a function of this process. The child gets the
 * step log on stdout+stderr, the parent paints; identical to what
 * osr_run_step does for a command, minus the exec. */
/* osr_step_try -- osr_step in a child, so a failing step reports instead of
 * ending the run. That is exactly what `( run_step "..." <verb> )` was in the
 * shell tier: the subshell is what kept run_step's error() from taking the
 * whole install with it, and it is used for the one shape that needs it -- an
 * OPTIONAL package that only some distros carry. */
int osr_step_try(const char *desc, int (*fn)(void *ctx), void *ctx) {
    pid_t pid;
    int status;

    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) _exit(osr_step(desc, fn, ctx) ? 0 : 1);
    if (waitpid(pid, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

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
        detach_stdin();
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
/* Both verbs live in lib/service.c, the port of lib/service.sh: they are not
 * module-only, and the servicemap parsing they share belongs with them. */

/* --- files ---------------------------------------------------------------- */

/* tee_root -- `as_root tee [-a] <path> >/dev/null <<'EOF' ... EOF`: write, or
 * append to, a file this program OWNS at a path only root can write -- a
 * .desktop entry, an apt source list, a PAM stack line.
 *
 * tee and not a plain write because the escalation is the whole point, and the
 * heredoc becomes a temp file handed to tee on stdin: sh's heredoc was one too,
 * so this is the same shape and not an extra command in anybody's log. */
static int tee_root(const char *path, const char *text, int append) {
    char tmpl[] = "/tmp/osr-tee.XXXXXX";
    char *argv[4];
    size_t len = strlen(text);
    int fd, rc;

    fd = mkstemp(tmpl);
    if (fd < 0) return 0;
    if (len > 0 && (size_t)write(fd, text, len) != len) {
        close(fd);
        (void)unlink(tmpl);
        return 0;
    }
    close(fd);
    fd = open(tmpl, O_RDONLY);
    if (fd < 0) { (void)unlink(tmpl); return 0; }
    argv[0] = (char *)"tee";
    argv[1] = append ? (char *)"-a" : (char *)path;
    argv[2] = append ? (char *)path : NULL;
    argv[3] = NULL;
    rc = osr_run_root_quiet_in(argv, fd);
    close(fd);
    (void)unlink(tmpl);
    return rc == 0;
}

int osr_write_root(const char *path, const char *text)  { return tee_root(path, text, 0); }
int osr_append_root(const char *path, const char *text) { return tee_root(path, text, 1); }

/* tee_user -- the same as the RICED ACCOUNT, for a file under its own $HOME
 * that this program owns and rewrites (a portal preference, a generated
 * fragment). Identity matters more than privilege here: a root-owned dotfile is
 * one the user's session cannot rewrite. */
static int tee_user(const char *path, const char *text, int append) {
    char tmpl[] = "/tmp/osr-tee.XXXXXX";
    char *argv[4];
    size_t len = strlen(text);
    int fd, rc;

    fd = mkstemp(tmpl);
    if (fd < 0) return 0;
    if (len > 0 && (size_t)write(fd, text, len) != len) {
        close(fd);
        (void)unlink(tmpl);
        return 0;
    }
    close(fd);
    fd = open(tmpl, O_RDONLY);
    if (fd < 0) { (void)unlink(tmpl); return 0; }
    argv[0] = (char *)"tee";
    argv[1] = append ? (char *)"-a" : (char *)path;
    argv[2] = append ? (char *)path : NULL;
    argv[3] = NULL;
    rc = osr_run_user_quiet_in(argv, fd);
    close(fd);
    (void)unlink(tmpl);
    return rc == 0;
}

int osr_write_user(const char *path, const char *text)  { return tee_user(path, text, 0); }
int osr_append_user(const char *path, const char *text) { return tee_user(path, text, 1); }

int osr_mkdir_p_all(const char *const dirs[]) {
    char **argv;
    size_t n = 0, i;
    int rc;

    while (dirs[n] != NULL) n++;
    argv = (char **)malloc((n + 3) * sizeof(char *));
    if (argv == NULL) osr_die_oom();
    argv[0] = (char *)"mkdir";
    argv[1] = (char *)"-p";
    for (i = 0; i < n; i++) argv[2 + i] = (char *)dirs[i];
    argv[2 + n] = NULL;
    rc = osr_run_user(argv);
    free(argv);
    return rc == 0;
}

int osr_mkdir_p(const char *dir) {
    const char *one[2];
    one[0] = dir; one[1] = NULL;
    return osr_mkdir_p_all(one);
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

#else /* _WIN32 */

/* --- the Windows body ------------------------------------------------------
 *
 * Four differences are worth knowing before reading it, because they are why
 * some of these bodies are much shorter than their POSIX twins:
 *
 * PRIVILEGE IS PER PROCESS, NOT PER COMMAND. There is no sudo. A run that
 * needs Administrator elevates itself once, up front (lib/elevate.h relaunches
 * the whole process under the `runas` verb), and everything after that point
 * is already elevated. So as_root and as_user -- osr_run_root, osr_run_user
 * and their quiet/capturing variants -- are all the same act here: run it as
 * whoever we are. They stay distinct FUNCTIONS because the module calling them
 * still means two different things, and because the profile a config lands in
 * is a separate question that osr_mod_home answers (an elevated child is told
 * the riced user's home through --user-home; see elevate.h).
 *
 * THERE IS NO FORK. osr_step's POSIX body runs the callback in a forked child
 * so its output can be captured into the live window; here it is called
 * directly, with its description printed first. A step is still a step in the
 * log; it just is not isolated from the process that ran it.
 *
 * A COMMAND IS A LINE, NOT A VECTOR. CreateProcess takes one command line and
 * the callee re-splits it, so every argv a module hands over is joined and
 * quoted here (win_cmdline). That is the one lossy step in this body, and it
 * is why the quoting rule is spelled out at that function rather than assumed.
 *
 * THERE IS ONE INIT SYSTEM. osr_service_enable drives the SCM through sc.exe
 * and lib/servicemap/ carries no Windows file, because a Windows service's
 * name IS the name you use -- the problem servicemap exists to solve does not
 * arise.
 * ------------------------------------------------------------------------- */

#include "common.h"
#include "module.h"
#include "ui.h"
#include "render.h"
#include "elevate.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>

/* --- what the module was given --------------------------------------------
 *
 * Read out of the environment, exactly as the POSIX body reads them, so a
 * module sees one contract. `osr detect` (lib/windetect.c) is what puts them
 * there, and osr.c's startup resolves OSR_ROOT/OSR_DOTFILES from the
 * executable's own location when nothing else has.
 * ------------------------------------------------------------------------- */
const char *osr_mod_root(void)      { return env_str("OSR_ROOT", "."); }
const char *osr_mod_dotfiles(void)  { return env_str("OSR_DOTFILES", ".."); }
const char *osr_mod_user(void)      { return env_str("OSR_USER", env_str("USERNAME", "")); }
const char *osr_mod_home(void)      { return env_str("OSR_HOME", env_str("USERPROFILE", "")); }
const char *osr_mod_theme(void)     { return env_str("OSR_THEME", ""); }
const char *osr_mod_theme_dir(void) { return env_str("OSR_THEME_DIR", ""); }
const char *osr_mod_pkg(void)       { return env_str("OSR_PKG", "windows"); }
const char *osr_mod_distro(void)    { return env_str("OSR_DISTRO", "windows"); }
/* There is exactly one service manager here, and it has been the same one
 * since NT: the SCM, driven through sc.exe. lib/servicemap/ has no Windows
 * file for the same reason -- nothing to choose between. */
const char *osr_mod_init(void)      { return env_str("OSR_INIT", "scm"); }

/* --- theme-only mode -------------------------------------------------------
 * The section 6a flag, and the same enumerated list the POSIX side keeps: the
 * mutating verbs below check it and become logging no-ops, so what survives a
 * `--theme-only` run is the file copying, which is what a theme is.
 * ------------------------------------------------------------------------- */
static int theme_only = 0;

int osr_theme_only(void) { return theme_only; }
void osr_set_theme_only(int on) { theme_only = on ? 1 : 0; }

int osr_theme_only_skip(const char *verb) {
    osr_debugf("theme-only: skipping %s", verb);
    return 1;
}

/* --- composing a command line ---------------------------------------------- */

/* win_cmdline -- join argv into the single command line CreateProcess and the
 * C runtime's spawn family take.
 *
 * An argument is quoted when it contains a space, a tab or a quote, and an
 * embedded quote is doubled -- which is what cmd.exe and the CRT's own
 * argument parser agree on for the cases this tree produces (paths, package
 * ids, flags). It is deliberately not a general Win32 quoting routine: the
 * backslash-before-quote rule only matters for arguments ending in a
 * backslash inside quotes, and a path that has to survive that is passed
 * through a file, not a command line.
 *
 * Returns 0 (leaving out empty) if the result would not fit, because a
 * truncated command line is a different command.
 */
static int win_cmdline(char *out, unsigned long out_sz, char *const argv[]) {
    unsigned long len = 0;
    int i;

    if (out_sz == 0) return 0;
    out[0] = '\0';

    for (i = 0; argv[i] != NULL; i++) {
        const char *a = argv[i];
        int needs_quote = (*a == '\0') || strpbrk(a, " \t\"") != NULL;
        const char *p;

        if (i > 0) {
            if (len + 1 >= out_sz) { out[0] = '\0'; return 0; }
            out[len++] = ' ';
        }
        if (needs_quote) {
            if (len + 1 >= out_sz) { out[0] = '\0'; return 0; }
            out[len++] = '"';
        }
        for (p = a; *p != '\0'; p++) {
            if (*p == '"') {
                if (len + 1 >= out_sz) { out[0] = '\0'; return 0; }
                out[len++] = '"';
            }
            if (len + 1 >= out_sz) { out[0] = '\0'; return 0; }
            out[len++] = *p;
        }
        if (needs_quote) {
            if (len + 1 >= out_sz) { out[0] = '\0'; return 0; }
            out[len++] = '"';
        }
    }
    out[len] = '\0';
    return 1;
}

#define OSR_WIN_CMD_MAX 4096

/* --- running things --------------------------------------------------------
 *
 * osr_run is the base: spawn argv, wait, hand back its exit status. Every
 * other runner here is that plus a redirection, and the as_root/as_user pairs
 * are that unchanged -- see this file's header on why.
 * ------------------------------------------------------------------------- */

/* spawn_redirected -- run argv with stdin/stdout/stderr optionally pointed
 * somewhere else. Each of in_fd/out_fd/err_fd is a descriptor to use, or -1
 * to leave that stream alone. The saved descriptors are restored before
 * returning, so a caller's own streams survive.
 */
static int spawn_redirected(char *const argv[], int in_fd, int out_fd, int err_fd) {
    int saved[3];
    int wants[3];
    intptr_t rc;
    int i;

    wants[0] = in_fd; wants[1] = out_fd; wants[2] = err_fd;

    fflush(stdout);
    fflush(stderr);

    for (i = 0; i < 3; i++) {
        saved[i] = -1;
        if (wants[i] < 0) continue;
        saved[i] = _dup(i);
        _dup2(wants[i], i);
    }

    rc = _spawnvp(_P_WAIT, argv[0], (const char *const *)argv);

    for (i = 0; i < 3; i++) {
        if (saved[i] < 0) continue;
        _dup2(saved[i], i);
        _close(saved[i]);
    }

    return (rc < 0) ? 127 : (int)rc;
}

/* devnull -- an open descriptor on NUL, or -1. The caller closes it. */
static int devnull(void) { return _open("NUL", _O_WRONLY); }

int osr_run(char *const argv[]) { return spawn_redirected(argv, -1, -1, -1); }

/* as_root / as_user: the same thing here. Kept as separate entry points
 * because a module still means two different things by them, and because
 * that is what makes a module one file across both systems. */
int osr_run_root(char *const argv[]) { return osr_run(argv); }
int osr_run_user(char *const argv[]) { return osr_run(argv); }

static int run_quiet_as(char *const argv[]) {
    int null_fd = devnull();
    int rc = spawn_redirected(argv, -1, null_fd, null_fd);
    if (null_fd >= 0) _close(null_fd);
    return rc;
}

int osr_run_root_quiet(char *const argv[]) { return run_quiet_as(argv); }
int osr_run_user_quiet(char *const argv[]) { return run_quiet_as(argv); }

int osr_run_user_in(char *const argv[], int in_fd) {
    return spawn_redirected(argv, in_fd, -1, -1);
}

int osr_run_root_in(char *const argv[], int in_fd) {
    return spawn_redirected(argv, in_fd, -1, -1);
}

static int run_quiet_in(char *const argv[], int in_fd) {
    int null_fd = devnull();
    int rc = spawn_redirected(argv, in_fd, null_fd, -1);
    if (null_fd >= 0) _close(null_fd);
    return rc;
}

int osr_run_user_quiet_in(char *const argv[], int in_fd) { return run_quiet_in(argv, in_fd); }
int osr_run_root_quiet_in(char *const argv[], int in_fd) { return run_quiet_in(argv, in_fd); }

int osr_have_cmd(const char *name) { return osr_path_lookup(name, NULL); }

/* osr_initramfs_regen -- Windows has no initramfs; nothing to rebuild. */
int osr_initramfs_regen(void) { return 1; }

/* osr_setcap -- POSIX file capabilities have no Windows equivalent: a
 * privilege here belongs to a token, not to a file, so there is nothing to
 * grant. Best-effort by contract on both sides (lib/module.h), so this is a
 * quiet 0 rather than a warning on every run of a module that asks. */
int osr_setcap(const char *caps, const char *cmd) {
    (void)caps;
    osr_debugf("setcap: not a Windows concept, %s left as installed", cmd);
    return 0;
}

/* --- capturing output ------------------------------------------------------
 *
 * _popen rather than a pipe assembled by hand: the command has already been
 * joined into a line by the time it gets here, and _popen is what runs a line
 * and gives back a stream.
 * ------------------------------------------------------------------------- */
static int capture(char *const argv[], Str *out, int merge_err) {
    char cmd[OSR_WIN_CMD_MAX];
    char buf[512];
    FILE *fp;
    int rc;

    if (!win_cmdline(cmd, sizeof(cmd), argv)) return 0;
    if (merge_err) {
        if (strlen(cmd) + 6 >= sizeof(cmd)) return 0;
        strcat(cmd, " 2>&1");
    } else {
        if (strlen(cmd) + 9 >= sizeof(cmd)) return 0;
        strcat(cmd, " 2>NUL");
    }

    fp = _popen(cmd, "r");
    if (fp == NULL) return 0;
    while (fgets(buf, (int)sizeof(buf), fp) != NULL) str_addz(out, buf);
    rc = _pclose(fp);
    return rc == 0;
}

int osr_run_capture(char *const argv[], Str *out)      { return capture(argv, out, 0); }
int osr_run_capture_err(char *const argv[], Str *out)  { return capture(argv, out, 1); }
int osr_run_root_capture(char *const argv[], Str *out) { return capture(argv, out, 1); }
int osr_run_user_capture(char *const argv[], Str *out) { return capture(argv, out, 0); }

/* --- steps -----------------------------------------------------------------
 *
 * osr_run_step keeps the sh run_step's fatality, which lib/module.h documents
 * as part of the contract: a module must not limp on past a mutation that only
 * half applied. osr_run_step_cmd (lib/ui.h) does NOT end the run, because
 * its own callers -- the package dispatch, the builders -- report and carry
 * on; the difference between the two is exactly this function.
 * ------------------------------------------------------------------------- */
int osr_run_step(const char *desc, char *const argv[]) {
    char cmd[OSR_WIN_CMD_MAX];

    if (!win_cmdline(cmd, sizeof(cmd), argv)) osr_die("%s failed (command line too long)", desc);
    if (osr_run_step_cmd(desc, cmd) != 0) osr_die("%s failed", desc);
    return 1;
}

int osr_run_step_root(const char *desc, char *const argv[]) { return osr_run_step(desc, argv); }
int osr_run_step_user(const char *desc, char *const argv[]) { return osr_run_step(desc, argv); }

/* osr_step -- the live window around a function of this program. With no fork
 * to isolate it in, the callback runs here and its output goes straight to the
 * log; the step is still announced and still fatal on failure, which is what
 * its callers depend on. */
int osr_step(const char *desc, int (*fn)(void *ctx), void *ctx) {
    osr_infof("%s", desc);
    if (!fn(ctx)) osr_die("%s failed", desc);
    return 1;
}

/* osr_step_try -- the same, non-fatal: the shape that exists for an OPTIONAL
 * package only some machines carry. */
int osr_step_try(const char *desc, int (*fn)(void *ctx), void *ctx) {
    osr_infof("%s", desc);
    if (!fn(ctx)) {
        osr_warnf("%s failed -- continuing", desc);
        return 0;
    }
    return 1;
}

/* --- files -----------------------------------------------------------------
 *
 * Every write here is a plain write. The POSIX bodies route theirs through
 * `sudo -u` or `sudo tee` because the installer may be running as a different
 * account than the one being riced; the Windows equivalent of that split is
 * the profile a path resolves in (osr_mod_home, fed by --user-home across an
 * elevation boundary), not the identity of the writing call.
 * ------------------------------------------------------------------------- */

/* dir_of -- the directory part of a path, either separator. */
static void dir_of(Str *out, const char *path) {
    const char *fwd = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *slash = fwd;

    if (back != NULL && (slash == NULL || back > slash)) slash = back;
    str_reset(out);
    if (slash == NULL) str_addc(out, '.');
    else str_add(out, path, (size_t)(slash - path));
}

int osr_mkdir_p(const char *dir) {
    Str part;
    const char *p;
    int ok = 1;

    if (dir == NULL || *dir == '\0') return 0;

    str_init(&part);
    for (p = dir; ; p++) {
        if (*p == '/' || *p == '\\' || *p == '\0') {
            /* Skip a bare drive letter ("C:") and the leading component of a
             * UNC path: neither is a directory that can be created. */
            if (part.len > 0 && !(part.len == 2 && part.p[1] == ':')) {
                if (_mkdir(str_text(&part)) != 0 && errno != EEXIST) {
                    if (!dir_exists(str_text(&part))) ok = 0;
                }
            }
            if (*p == '\0') break;
        }
        str_addc(&part, *p);
    }
    str_free(&part);
    return ok;
}

int osr_mkdir_p_all(const char *const dirs[]) {
    size_t i;
    int ok = 1;
    for (i = 0; dirs[i] != NULL; i++) {
        if (!osr_mkdir_p(dirs[i])) ok = 0;
    }
    return ok;
}

/* write_file -- the body behind all four of the write/append entry points.
 * Content is written verbatim, so a caller that wants a trailing newline
 * includes one -- the same rule lib/module.h states for the POSIX pair. */
static int write_file(const char *path, const char *text, int append) {
    Str dir;
    FILE *fp;
    size_t len = strlen(text);
    int ok;

    str_init(&dir);
    dir_of(&dir, path);
    osr_mkdir_p(str_text(&dir));
    str_free(&dir);

    fp = fopen(path, append ? "ab" : "wb");
    if (fp == NULL) {
        osr_warnf("cannot write %s", path);
        return 0;
    }
    ok = (len == 0) || (fwrite(text, 1, len, fp) == len);
    fclose(fp);
    if (!ok) osr_warnf("short write to %s", path);
    return ok;
}

int osr_write_root(const char *path, const char *text)  { return write_file(path, text, 0); }
int osr_append_root(const char *path, const char *text) { return write_file(path, text, 1); }
int osr_write_user(const char *path, const char *text)  { return write_file(path, text, 0); }
int osr_append_user(const char *path, const char *text) { return write_file(path, text, 1); }

/* osr_install_file -- backup_copy: back dst up to dst.bak once, then copy,
 * skipping the write entirely when the contents already match. The skip is
 * not an optimization -- it is what keeps a rerun from rewriting a file whose
 * mtime other things watch (section 2). */
int osr_install_file(const char *src, const char *dst) {
    Str bak;
    int ok;

    if (!file_exists(src)) {
        osr_warnf("install: %s does not exist", src);
        return 0;
    }
    if (osr_files_equal(src, dst)) return 1;

    if (file_exists(dst)) {
        str_init(&bak);
        str_addz(&bak, dst);
        str_addz(&bak, ".bak");
        if (!file_exists(str_text(&bak))) {
            /* Once, ever: the .bak is what the machine looked like BEFORE
             * os-rice touched it, and overwriting it on the second run would
             * throw that away in favour of os-rice's own last output. */
            CopyFileA(dst, str_text(&bak), FALSE);
        }
        str_free(&bak);
    }

    ok = osr_copy_file(src, dst);
    if (!ok) osr_warnf("install: could not write %s", dst);
    return ok;
}

int osr_install_layer(const char *src, const char *dst) { return osr_install_file(src, dst); }

/* seed -- write once when absent, then never again: the "seeded, then yours"
 * contract (section 5). Already present counts as success. */
static int seed(const char *dst, const char *content) {
    if (file_exists(dst)) return 1;
    return write_file(dst, content, 0);
}

int osr_seed_file(const char *dst, const char *content)      { return seed(dst, content); }
int osr_seed_file_root(const char *dst, const char *content) { return seed(dst, content); }

int osr_ensure_line(const char *file, const char *line) {
    char *buf;
    size_t len, pos = 0;
    Line got;
    size_t want = strlen(line);
    int present = 0;

    buf = slurp(file, &len);
    if (buf != NULL) {
        while (next_line(buf, len, &pos, &got)) {
            if (got.len == want && memcmp(got.start, line, want) == 0) { present = 1; break; }
        }
        free(buf);
    }
    if (present) return 1;

    {
        Str text;
        int ok;
        str_init(&text);
        /* A file that does not end in a newline would otherwise get this line
         * glued onto its last one. */
        if (buf != NULL && len > 0) {
            char *tail = slurp(file, &len);
            if (tail != NULL) {
                if (len > 0 && tail[len - 1] != '\n') str_addc(&text, '\n');
                free(tail);
            }
        }
        str_addz(&text, line);
        str_addc(&text, '\n');
        ok = write_file(file, str_text(&text), 1);
        str_free(&text);
        return ok;
    }
}

/* osr_install_theme_layer -- the current theme's version of <app>/<name> into
 * dst, whether the theme ships the file itself or the dotfiles template has to
 * be rendered for it (lib/render.c's osr_theme_source decides which, the same
 * way on both systems). Returns 0 when this theme has neither, which is the
 * caller's cue to fall back to the dotfiles default -- so a miss here is not a
 * failure. */
int osr_install_theme_layer(const char *app, const char *name, const char *dst) {
    Str src;
    int is_temp = 0;
    int ok;

    str_init(&src);
    if (!osr_theme_source(&src, app, name, &is_temp)) {
        str_free(&src);
        return 0;
    }
    ok = osr_install_file(str_text(&src), dst);
    if (is_temp) remove(str_text(&src));
    str_free(&src);
    return ok;
}

#endif /* _WIN32 */
