/* lib/user.c -- the C behind lib/user.sh: the target-user model (§8) and
 * the config-file primitives every module writes through.
 *
 * What is here is every decision:
 *
 *   passwd <user>            the /etc/passwd line (NSS first, file second)
 *   shell <user>             field 7
 *   realpath <path>          `readlink -f`, with its fallbacks
 *   shell-is <user> <shell>  exit 0 when the account already uses it
 *   resolve [user]           OSR_USER / OSR_HOME, as shell assignments
 *   shell-registered <shell> exit 0 when /etc/shells already lists it
 *   passwd-shell <u> <sh>    the rewritten /etc/passwd, to stdout
 *   needs-line <file> <line> exit 0 when ensure_line would have to append
 *   compose-block <f> <name> the file with the marked region replaced (body
 *                            on stdin), to stdout
 *   same-content <a> <b>     exit 0 when the two files are byte-identical
 *
 * What is NOT here is every WRITE. as_user/as_root are shell functions that
 * shell out to `sudo -u`/`sudo`, and modules use them as command prefixes
 * (`as_root pacman -S ...`), so they cannot become anything else; and a write
 * performed here would land as whoever is running -- usually root -- in a
 * directory that must stay owned by the user being riced. So this composes
 * and decides, lib/user.sh performs.
 *
 * What the user model and the file primitives must do is stated in
 * test/unit_c/user_test.c.
 *
 * C89 + POSIX.
 */
/* _XOPEN_SOURCE, not just _POSIX_C_SOURCE: realpath() is XSI on glibc. */
#define _XOPEN_SOURCE 700

#include "common.h"
#include "cmds.h"
#include "config.h"
#include "module.h"

#include <limits.h>
#include <pwd.h>
#include <unistd.h>

/* The two system files this touches, overridable so a test can sandbox them --
 * the same trick lib/detect.sh has always used for /proc/meminfo and
 * /sys/class/drm. Setting OSR_PASSWD_FILE also turns NSS off: a sandbox that
 * says "this box has one user called tester" must not be answered from the
 * real machine's directory service. */
static const char *passwd_path(void) { return env_str("OSR_PASSWD_FILE", "/etc/passwd"); }
static const char *shells_path(void) { return env_str("OSR_SHELLS_FILE", "/etc/shells"); }
static int nss_allowed(void) { return env_str("OSR_PASSWD_FILE", NULL) == NULL; }

/* passwd_line -- what `getent passwd <user>` prints, or the matching line of
 * /etc/passwd when there is no NSS entry. getpwnam IS the NSS lookup getent
 * performs, so the two agree on LDAP/SSSD accounts the file does not have. */
static int passwd_line(Str *out, const char *user) {
    struct passwd *pw = nss_allowed() ? getpwnam(user) : NULL;
    char *buf;
    size_t len;
    size_t pos = 0;
    Line line;
    size_t ulen;

    if (pw != NULL) {
        str_addz(out, pw->pw_name);
        str_addc(out, ':');
        str_addz(out, pw->pw_passwd != NULL ? pw->pw_passwd : "x");
        str_addc(out, ':');
        str_addl(out, (long)pw->pw_uid);
        str_addc(out, ':');
        str_addl(out, (long)pw->pw_gid);
        str_addc(out, ':');
        str_addz(out, pw->pw_gecos != NULL ? pw->pw_gecos : "");
        str_addc(out, ':');
        str_addz(out, pw->pw_dir != NULL ? pw->pw_dir : "");
        str_addc(out, ':');
        str_addz(out, pw->pw_shell != NULL ? pw->pw_shell : "");
        str_addc(out, '\n');
        return 1;
    }

    /* busybox/Alpine fallback: `grep "^$1:" /etc/passwd` */
    buf = slurp(passwd_path(), &len);
    if (buf == NULL) return 0;
    ulen = strlen(user);
    while (next_line(buf, len, &pos, &line)) {
        if (line.len > ulen && strncmp(line.start, user, ulen) == 0 && line.start[ulen] == ':') {
            str_add(out, line.start, line.len);
            str_addc(out, '\n');
            free(buf);
            return 1;
        }
    }
    free(buf);
    return 0;
}

/* passwd_field -- one colon-separated field of a passwd line, 1-based, as
 * `cut -d: -f<n>` would give it. Returns 0 when there is no passwd line:
 * `cut` prints nothing for empty input, not an empty line. */
static int passwd_field(Str *out, const char *user, int field) {
    Str line;
    const char *p;
    const char *start;
    int n = 1;

    str_init(&line);
    if (!passwd_line(&line, user)) { str_free(&line); return 0; }
    str_trim_trailing(&line, '\n');
    start = str_text(&line);
    for (p = start; ; p++) {
        if (*p == ':' || *p == '\0') {
            if (n == field) {
                str_add(out, start, (size_t)(p - start));
                break;
            }
            if (*p == '\0') break;
            n++;
            start = p + 1;
        }
    }
    str_free(&line);
    return 1;
}

/* canon -- `readlink -f "$1" 2>/dev/null || printf '%s\n' "$1"`.
 *
 * readlink -f resolves a path whose LAST component does not exist yet, and
 * fails outright when an earlier one is missing; realpath(3) refuses both, so
 * the missing-leaf case is retried against the parent directory. */
static void canon(Str *out, const char *path) {
    char buf[PATH_MAX];
    const char *slash;

    if (realpath(path, buf) != NULL) {
        str_addz(out, buf);
        return;
    }
    slash = strrchr(path, '/');
    if (slash != NULL) {
        Str dir;
        str_init(&dir);
        str_add(&dir, path, (size_t)(slash - path));
        if (dir.len == 0) str_addc(&dir, '/');
        if (realpath(str_text(&dir), buf) != NULL) {
            str_addz(out, buf);
            if (out->len > 0 && out->p[out->len - 1] != '/') str_addc(out, '/');
            str_addz(out, slash + 1);
            str_free(&dir);
            return;
        }
        str_free(&dir);
    }
    str_addz(out, path); /* the `||` branch: the input, unchanged */
}

/* cmd_shell_is -- osr_shell_is: true when <user> already logs in with
 * <shell>, comparing canonical paths so the /bin -> /usr/bin symlink split
 * does not cause a pointless reset. */
static int cmd_shell_is(const char *user, const char *shell) {
    Str cur;
    Str a;
    Str b;
    int same;

    str_init(&cur);
    passwd_field(&cur, user, 7);
    if (cur.len == 0) { str_free(&cur); return 1; }
    if (strcmp(str_text(&cur), shell) == 0) { str_free(&cur); return 0; }

    str_init(&a);
    str_init(&b);
    canon(&a, str_text(&cur));
    canon(&b, shell);
    same = strcmp(str_text(&a), str_text(&b)) == 0;
    str_free(&a);
    str_free(&b);
    str_free(&cur);
    return same ? 0 : 1;
}

/* cmd_resolve -- osr_resolve_user: which account is being riced, and where it
 * lives. Order (§8): --user > $SUDO_USER (when invoked via sudo) > $USER >
 * whoever we are. */
/* resolve_user -- who is being riced, and where they live. Order (§8):
 * --user > $SUDO_USER (when invoked via sudo) > $USER > whoever we are. */
static void resolve_user(Str *user, Str *home, const char *explicit_user) {
    const char *sudo_user;

    if (explicit_user != NULL && *explicit_user != '\0') {
        str_addz(user, explicit_user);
    } else if ((sudo_user = env_str("SUDO_USER", NULL)) != NULL && strcmp(sudo_user, "root") != 0) {
        str_addz(user, sudo_user);
    } else if (env_str("USER", NULL) != NULL) {
        str_addz(user, env_str("USER", ""));
    } else {
        struct passwd *pw = nss_allowed() ? getpwuid(getuid()) : NULL; /* sh: id -un */
        str_addz(user, (pw != NULL && pw->pw_name != NULL) ? pw->pw_name : "root");
    }

    /* The real home (field 6); handles /root and non-standard homes. */
    passwd_field(home, str_text(user), 6);
    if (home->len == 0) {
        if (strcmp(str_text(user), "root") == 0) {
            str_addz(home, "/root");
        } else {
            str_addz(home, "/home/");
            str_addz(home, str_text(user));
        }
    }
}

/* osr_resolve_user -- the same, into this process's environment, for the runner
 * and every child it forks. `export` is what the shim bought; setenv is that. */
void osr_resolve_user(const char *explicit_user) {
    Str user, home;
    str_init(&user); str_init(&home);
    resolve_user(&user, &home, explicit_user);
    setenv("OSR_USER", str_text(&user), 1);
    setenv("OSR_HOME", str_text(&home), 1);
    str_free(&user); str_free(&home);
}

static int cmd_resolve(const char *explicit_user) {
    Str user, home, out;

    str_init(&user); str_init(&home);
    resolve_user(&user, &home, explicit_user);
    str_init(&out);
    sh_assign(&out, "OSR_USER", str_text(&user));
    sh_assign(&out, "OSR_HOME", str_text(&home));
    out_flush(&out);
    str_free(&out);
    str_free(&home);
    str_free(&user);
    return 0;
}

/* cmd_shell_registered -- `grep -qx -- "$1" /etc/shells`: is this shell
 * already a valid login shell? An unlisted one makes chsh refuse for
 * non-root and makes some login managers treat the account as broken. */
static int cmd_shell_registered(const char *shell) {
    char *buf;
    size_t len;
    size_t pos = 0;
    Line line;
    size_t slen = strlen(shell);
    int found = 0;

    if (*shell == '\0') return 0;               /* sh: `[ -n "$1" ] || return 0` */
    buf = slurp(shells_path(), &len);
    if (buf == NULL) return 1;
    while (!found && next_line(buf, len, &pos, &line)) {
        if (line.len == slen && strncmp(line.start, shell, slen) == 0) found = 1;
    }
    free(buf);
    return found ? 0 : 1;
}

/* cmd_passwd_shell -- the /etc/passwd this account's login shell change would
 * produce: field 7 rewritten for one user, every other byte untouched (the sh
 * version's awk -F: -v OFS=:). Exit 1 -- and no output -- when the user does
 * not live in /etc/passwd at all (NSS/LDAP), which is the sh version's
 * `grep -q ... || return 1`. */
static int compose_passwd_shell(Str *out, const char *path, const char *user,
                                const char *shell) {
    char *buf;
    size_t len;
    size_t pos = 0;
    Line line;
    size_t ulen = strlen(user);
    int found = 0;

    buf = slurp(path, &len);
    if (buf == NULL) return 0;

    while (next_line(buf, len, &pos, &line)) {
        if (line.len > ulen && strncmp(line.start, user, ulen) == 0 && line.start[ulen] == ':') {
            /* rewrite the 7th field, keep the first six as they are */
            size_t i;
            int colons = 0;
            size_t cut = line.len;
            found = 1;
            for (i = 0; i < line.len; i++) {
                if (line.start[i] == ':') {
                    colons++;
                    if (colons == 6) { cut = i + 1; break; }
                }
            }
            str_add(out, line.start, cut);
            if (colons == 6) str_addz(out, shell);
            str_addc(out, '\n');
        } else {
            str_add(out, line.start, line.len);
            str_addc(out, '\n');
        }
    }
    free(buf);
    return found;
}

/* cmd_passwd_shell -- the same, to stdout, for lib/user.sh's `osr user
 * passwd-shell`. Exit 1 -- and no output -- when the user does not live in
 * /etc/passwd at all. */
static int cmd_passwd_shell(const char *path, const char *user, const char *shell) {
    Str out;
    int found;
    str_init(&out);
    found = compose_passwd_shell(&out, path, user, shell);
    if (found) out_flush(&out);
    str_free(&out);
    return found ? 0 : 1;
}

/* --- the login shell, changed for real ------------------------------------
 *
 * lib/user.sh's osr_register_shell / osr_passwd_set_shell / osr_set_login_shell.
 * They stayed in sh only because they WRITE, and a write had to go through the
 * as_root shell function; module.h's osr_run_root is that same escalation
 * without a shell, so a C module can have them. `chsh` is not universal --
 * busybox has no applet and a minimal Fedora keeps it in util-linux-user -- so
 * each mechanism is tried and the RESULT verified rather than an exit code
 * trusted: chsh -> usermod -> a direct /etc/passwd rewrite.
 */

/* osr_user_shell_is -- cmd_shell_is as a predicate: 1 when the account already
 * logs in with this shell, canonical paths compared. */
int osr_user_shell_is(const char *user, const char *shell) {
    return cmd_shell_is(user, shell) == 0;
}

/* osr_register_shell -- make sure /etc/shells lists it (idempotent, §2). Not
 * every distro's zsh package registers itself, and an unlisted shell makes
 * chsh refuse for non-root. */
int osr_register_shell(const char *shell) {
    Str line;
    int ok;

    if (shell == NULL || *shell == '\0') return 1;
    if (cmd_shell_registered(shell) == 0) return 1;
    str_init(&line);
    str_addz(&line, shell);
    str_addc(&line, '\n');
    ok = osr_append_root(shells_path(), str_text(&line));
    str_free(&line);
    return ok;
}

/* osr_passwd_set_shell -- the last resort: rewrite field 7 in /etc/passwd.
 * Written through `cp -f` onto the existing file, never a rename, so the inode
 * keeps its mode, owner and SELinux context. */
static int osr_passwd_set_shell(const char *user, const char *shell) {
    char tmpl[] = "/tmp/osr-passwd.XXXXXX";
    Str body;
    char *argv[5];
    int fd, ok = 0;

    str_init(&body);
    if (!compose_passwd_shell(&body, passwd_path(), user, shell) || body.len == 0) {
        str_free(&body);
        return 0;
    }
    fd = mkstemp(tmpl);
    if (fd < 0) { str_free(&body); return 0; }
    if ((size_t)write(fd, str_text(&body), body.len) == body.len) {
        close(fd);
        argv[0] = (char *)"cp"; argv[1] = (char *)"-f"; argv[2] = tmpl;
        argv[3] = (char *)passwd_path(); argv[4] = NULL;
        ok = osr_run_root(argv) == 0;
    } else {
        close(fd);
    }
    (void)unlink(tmpl);
    str_free(&body);
    return ok;
}

/* osr_set_login_shell -- set it whatever the box provides, and report whether
 * it actually took. Returns 0 when the shell still is not <shell> afterwards,
 * which is the caller's cue to warn rather than claim success. */
int osr_set_login_shell(const char *user, const char *shell) {
    char *argv[5];

    (void)osr_register_shell(shell);

    if (osr_have_cmd("chsh")) {
        argv[0] = (char *)"chsh"; argv[1] = (char *)"-s"; argv[2] = (char *)shell;
        argv[3] = (char *)user; argv[4] = NULL;
        (void)osr_run_root(argv);
    }
    if (!osr_user_shell_is(user, shell) && osr_have_cmd("usermod")) {
        argv[0] = (char *)"usermod"; argv[1] = (char *)"-s"; argv[2] = (char *)shell;
        argv[3] = (char *)user; argv[4] = NULL;
        (void)osr_run_root(argv);
    }
    if (!osr_user_shell_is(user, shell)) (void)osr_passwd_set_shell(user, shell);

    return osr_user_shell_is(user, shell);
}

/* cmd_needs_line -- ensure_line's test: `[ -f ] && grep -qF -- "$line"`,
 * inverted. Exit 0 means "append it". Note grep -F matches a SUBSTRING, not a
 * whole line, which is what makes re-running a module that appends a longer
 * line idempotent. */
static int cmd_needs_line(const char *path, const char *needle) {
    char *buf;
    size_t len;
    int found = 0;

    buf = slurp(path, &len);
    if (buf == NULL) return 0;                  /* no file -> yes, append */
    if (*needle == '\0') {
        found = 1;                              /* grep -F "" matches anything */
    } else {
        size_t nlen = strlen(needle);
        size_t i;
        for (i = 0; i + nlen <= len && !found; i++) {
            if (memcmp(buf + i, needle, nlen) == 0) found = 1;
        }
    }
    free(buf);
    return found ? 1 : 0;
}

/* cmd_compose_block -- ensure_block: own a marked region, rewriting only
 * between the markers (§5) and keeping every byte outside them. The body
 * arrives on stdin, as it did through the shell's heredoc. */
static int cmd_compose_block(const char *path, const char *name) {
    Str body;
    Str out;
    int c;

    /* `_eb_body=$(cat)` -- the body arrives on stdin, as it did through the
     * shell's heredoc. The composition itself lives in lib/config.c, which is
     * where ensure_block's callers are. */
    str_init(&body);
    while ((c = fgetc(stdin)) != EOF) str_addc(&body, (char)c);

    str_init(&out);
    osr_compose_block(&out, path, name, str_text(&body));
    out_flush(&out);
    str_free(&out);
    str_free(&body);
    return 0;
}

/* cmd_same_content -- `cmp -s <a> <b>`. */
static int cmd_same_content(const char *a, const char *b) {
    char *ba;
    char *bb;
    size_t la;
    size_t lb;
    int same;

    ba = slurp(a, &la);
    if (ba == NULL) return 1;
    bb = slurp(b, &lb);
    if (bb == NULL) { free(ba); return 1; }
    same = (la == lb) && (la == 0 || memcmp(ba, bb, la) == 0);
    free(ba);
    free(bb);
    return same ? 0 : 1;
}

static int usage(void) {
    fputs("usage: osr user <subcommand> [args]\n\n", stderr);
    fputs("  passwd <user>              the account's passwd line\n", stderr);
    fputs("  shell <user>               its login shell\n", stderr);
    fputs("  realpath <path>            canonical path (readlink -f)\n", stderr);
    fputs("  shell-is <user> <shell>    exit 0 when it already logs in with it\n", stderr);
    fputs("  resolve [user]             OSR_USER/OSR_HOME as shell assignments\n", stderr);
    fputs("  shell-registered <shell>   exit 0 when /etc/shells lists it\n", stderr);
    fputs("  passwd-shell <user> <sh>   /etc/passwd with field 7 rewritten\n", stderr);
    fputs("  passwd-shell-file <f> <u> <sh>  the same, against a named file\n", stderr);
    fputs("  needs-line <file> <line>   exit 0 when the line must be appended\n", stderr);
    fputs("  compose-block <file> <name>  marked region rewritten (body: stdin)\n", stderr);
    fputs("  same-content <a> <b>       exit 0 when the files are identical\n", stderr);
    return 2;
}

int osr_user_main(int argc, char **argv) {
    if (argc < 2) return usage();

    if (strcmp(argv[1], "passwd") == 0 && argc == 3) {
        Str out;
        int ok;
        str_init(&out);
        ok = passwd_line(&out, argv[2]);
        out_flush(&out);
        str_free(&out);
        return ok ? 0 : 1;
    }
    if (strcmp(argv[1], "shell") == 0 && argc == 3) {
        Str out;
        str_init(&out);
        if (passwd_field(&out, argv[2], 7)) str_addc(&out, '\n'); /* cut's line */
        out_flush(&out);
        str_free(&out);
        return 0;
    }
    if (strcmp(argv[1], "realpath") == 0 && argc == 3) {
        Str out;
        str_init(&out);
        canon(&out, argv[2]);
        str_addc(&out, '\n');
        out_flush(&out);
        str_free(&out);
        return 0;
    }
    if (strcmp(argv[1], "shell-is") == 0 && argc == 4) return cmd_shell_is(argv[2], argv[3]);
    if (strcmp(argv[1], "resolve") == 0 && argc <= 3) {
        return cmd_resolve(argc == 3 ? argv[2] : "");
    }
    if (strcmp(argv[1], "shell-registered") == 0 && argc == 3) return cmd_shell_registered(argv[2]);
    if (strcmp(argv[1], "passwd-shell") == 0 && argc == 4) {
        return cmd_passwd_shell(passwd_path(), argv[2], argv[3]);
    }
    /* passwd-shell-file -- the same rewrite against a named file, so the
     * parity test can prove it without touching the real /etc/passwd. */
    if (strcmp(argv[1], "passwd-shell-file") == 0 && argc == 5) {
        return cmd_passwd_shell(argv[2], argv[3], argv[4]);
    }
    if (strcmp(argv[1], "needs-line") == 0 && argc == 4) return cmd_needs_line(argv[2], argv[3]);
    if (strcmp(argv[1], "compose-block") == 0 && argc == 4) return cmd_compose_block(argv[2], argv[3]);
    if (strcmp(argv[1], "same-content") == 0 && argc == 4) return cmd_same_content(argv[2], argv[3]);
    return usage();
}
