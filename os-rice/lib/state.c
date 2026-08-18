/* lib/state.c -- the C behind lib/state.sh: what is currently applied.
 *
 * ~/.config/osr/state, `key=value`, one per line. Small on purpose: this is
 * not a database, it is the four answers something outside the installer
 * needs (rice, theme, wallpaper, applied). It is user-owned data, not config:
 * a missing or corrupt state file is never fatal -- every reader degrades to
 * "unknown" and the system still applies.
 *
 * Subcommands:
 *
 *   file            the path, no trailing newline (sh: printf '%s')
 *   get <key>       the value, "" when unset or the file is missing
 *   set <k> <v>     write one key, preserving the others
 *   compose <k> <v> the WHOLE new file, to stdout (what `set` writes)
 *
 * `set` performs the write itself, including the escalation: the file belongs
 * to the account being riced, not to whoever is running the installer
 * (user-for-user, §8), so writing it as root would leave a root-owned file in
 * the user's config dir. That is what lib/state.sh's `as_user mkdir -p` +
 * `as_user tee` did, and as_user_write below is the same two steps -- run
 * directly when we already are $OSR_USER, through `sudo -u` when we are not.
 * With that here, lib/state.sh had nothing left to do and is gone.
 *
 * The sh original matched keys with sed and grep, i.e. as BASIC REGULAR
 * EXPRESSIONS (`osr_state_get "wallpaper.$OSR_THEME"` really does treat that
 * dot as "any character"), so this uses regcomp/regexec in BRE mode rather
 * than a literal compare -- same matches, quirks included. A key that is not
 * a valid BRE is the one place this deliberately differs: sed/grep would
 * error out, which in `set`'s case silently truncated the file to one line;
 * here it reads as "matches nothing", so the file survives.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "cmds.h"

#include <fcntl.h>
/* tcc (0.9.27) reports __STDC_VERSION__ as C99 whatever -std= asks for, so
 * glibc's regex.h takes the branch that sizes regexec's __pmatch with a VLA
 * parameter -- which tcc then cannot parse ("'__nmatch' undeclared"). The
 * header leaves this macro as the override hook; empty is its own pre-C99
 * spelling, and that size was only ever documentation. */
#ifndef _REGEX_NELTS
#define _REGEX_NELTS(n)
#endif
#include <regex.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* state_path -- "${OSR_HOME:-$HOME}/.config/osr/state". OSR_HOME is what
 * makes the tests hermetic and what user-for-user installs point at. */
static void state_path(Str *out) {
    str_addz(out, env_str("OSR_HOME", env_str("HOME", "")));
    str_addz(out, "/.config/osr/state");
}

/* key_regex -- compile "^<key>=" the way sed's `s/^KEY=//p` and grep's
 * `-v "^KEY="` read it: a BRE. Returns 0 when the key is not one. */
static int key_regex(regex_t *re, const char *key) {
    Str pat;
    int rc;
    str_init(&pat);
    str_addc(&pat, '^');
    str_addz(&pat, key);
    str_addc(&pat, '=');
    rc = regcomp(re, str_text(&pat), 0); /* 0 = POSIX basic, as in sed/grep */
    str_free(&pat);
    return rc == 0;
}

/* cmd_get -- sed -n "s/^KEY=//p" <file> | tail -n 1.
 *
 * Last assignment wins, matching how the file is rewritten. The trailing
 * newline is carried over from the matched line: sed does not add one to a
 * file that ended without it, and neither does tail.
 */
static int cmd_get(const char *key) {
    Str path;
    char *buf;
    size_t len;
    size_t pos = 0;
    Line line;
    regex_t re;
    Str value;
    int found = 0;
    int found_newline = 0;

    str_init(&path);
    state_path(&path);
    buf = slurp(str_text(&path), &len);
    str_free(&path);
    if (buf == NULL) return 0;
    if (!key_regex(&re, key)) { free(buf); return 0; }

    str_init(&value);
    while (next_line(buf, len, &pos, &line)) {
        regmatch_t m;
        char saved = line.start[line.len];
        ((char *)line.start)[line.len] = '\0';
        if (regexec(&re, line.start, 1, &m, 0) == 0) {
            value.len = 0;
            str_add(&value, line.start + m.rm_eo, line.len - (size_t)m.rm_eo);
            found = 1;
            found_newline = line.had_newline;
        }
        ((char *)line.start)[line.len] = saved;
    }
    regfree(&re);

    if (found) {
        Str out;
        str_init(&out);
        str_add(&out, str_text(&value), value.len);
        if (found_newline) str_addc(&out, '\n');
        out_flush(&out);
        str_free(&out);
    }
    str_free(&value);
    free(buf);
    return 0;
}

/* cmd_compose -- the file lib/state.sh would have written: every line that
 * is not this key, then `key=value`. Mirrors the sh sequence exactly,
 * trailing-blank-line quirk included: the body went through a `$(...)`,
 * which eats trailing newlines, before being reprinted with one.
 */
static void compose(Str *out, const char *key, const char *value) {
    Str path;
    char *buf;
    size_t len;
    size_t pos = 0;
    Line line;
    regex_t re;
    int have_re;
    Str body;

    str_init(&path);
    state_path(&path);
    buf = slurp(str_text(&path), &len);
    str_free(&path);

    have_re = key_regex(&re, key);
    str_init(&body);
    if (buf != NULL && have_re) {
        int first = 1;
        while (next_line(buf, len, &pos, &line)) {
            regmatch_t m;
            char saved = line.start[line.len];
            ((char *)line.start)[line.len] = '\0';
            if (regexec(&re, line.start, 1, &m, 0) != 0) {
                if (!first) str_addc(&body, '\n');
                str_add(&body, line.start, line.len);
                first = 0;
            }
            ((char *)line.start)[line.len] = saved;
        }
    }
    if (have_re) regfree(&re);
    free(buf);

    /* `$(...)` ate the trailing newlines before the body was reprinted. */
    str_trim_trailing(&body, '\n');

    if (body.len > 0) {
        str_add(out, str_text(&body), body.len);
        str_addc(out, '\n');
    }
    str_addz(out, key);
    str_addc(out, '=');
    str_addz(out, value);
    str_addc(out, '\n');
    str_free(&body);
}

static int cmd_compose(const char *key, const char *value) {
    Str out;
    str_init(&out);
    compose(&out, key, value);
    out_flush(&out);
    str_free(&out);
    return 0;
}

/* target_user -- $OSR_USER, the account being riced; empty means "whoever is
 * running", which is what as_user did when the two were the same. */
static const char *target_user(void) { return env_str("OSR_USER", ""); }

/* need_sudo -- as_user's `[ "$(id -un)" = "$OSR_USER" ]` test, inverted. */
static int need_sudo(void) {
    const char *want = target_user();
    struct passwd *pw;
    if (*want == '\0') return 0;
    pw = getpwuid(getuid());
    if (pw == NULL || pw->pw_name == NULL) return 1;
    return strcmp(pw->pw_name, want) != 0;
}

/* run_as_user -- `as_user <argv...>`, optionally with content on its stdin.
 * Returns the child's exit status (or -1 if it could not be started). */
static int run_as_user(char **argv, const char *content, size_t len) {
    char *sudo_argv[8];
    char **use = argv;
    int fds[2];
    pid_t pid;
    int status;
    int i;

    if (need_sudo()) {
        sudo_argv[0] = (char *)"sudo";
        sudo_argv[1] = (char *)"-u";
        sudo_argv[2] = (char *)target_user();
        for (i = 0; argv[i] != NULL && i < 4; i++) sudo_argv[3 + i] = argv[i];
        sudo_argv[3 + i] = NULL;
        use = sudo_argv;
    }

    if (content != NULL && pipe(fds) != 0) return -1;
    pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (content != NULL) {
            dup2(fds[0], 0);
            close(fds[0]);
            close(fds[1]);
        }
        /* `>/dev/null`: tee echoes what it writes, and the sh version threw
         * that away. */
        {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) { dup2(devnull, 1); close(devnull); }
        }
        execvp(use[0], use);
        _exit(127);
    }
    if (content != NULL) {
        close(fds[0]);
        if (len > 0) {
            size_t off = 0;
            while (off < len) {
                long n = (long)write(fds[1], content + off, len - off);
                if (n <= 0) break;
                off += (size_t)n;
            }
        }
        close(fds[1]);
    }
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

/* mkdir_p -- `mkdir -p`, in-process, for the case where no escalation is
 * needed. Existing directories are not an error, as with -p. */
static int mkdir_p(const char *path) {
    Str partial;
    const char *p = path;
    int rc = 0;

    str_init(&partial);
    if (*p == '/') { str_addc(&partial, '/'); p++; }
    while (*p != '\0') {
        const char *slash = strchr(p, '/');
        size_t len = (slash != NULL) ? (size_t)(slash - p) : strlen(p);
        if (len > 0) {
            if (partial.len > 1 || (partial.len == 1 && partial.p[0] != '/')) str_addc(&partial, '/');
            str_add(&partial, p, len);
            if (mkdir(str_text(&partial), 0777) != 0 && !dir_exists(str_text(&partial))) rc = 1;
        }
        if (slash == NULL) break;
        p = slash + 1;
    }
    str_free(&partial);
    return rc;
}

/* dir_of -- `dirname "$path"` for the paths this writes (always absolute). */
static void dir_of(Str *out, const char *path) {
    const char *slash = strrchr(path, '/');
    if (slash == NULL) { str_addc(out, '.'); return; }
    if (slash == path) { str_addc(out, '/'); return; }
    str_add(out, path, (size_t)(slash - path));
}

/* cmd_set -- osr_state_set: compose the new file, then write it as the user.
 * `mkdir -p` first, exactly as the sh version did. */
static int cmd_set(const char *key, const char *value) {
    Str path;
    Str dir;
    Str content;
    int rc = 0;

    str_init(&path);
    state_path(&path);
    str_init(&dir);
    dir_of(&dir, str_text(&path));
    str_init(&content);
    compose(&content, key, value);

    if (need_sudo()) {
        char *mk[4];
        char *tee[3];
        mk[0] = (char *)"mkdir"; mk[1] = (char *)"-p"; mk[2] = dir.p; mk[3] = NULL;
        if (run_as_user(mk, NULL, 0) != 0) rc = 1;
        tee[0] = (char *)"tee"; tee[1] = path.p; tee[2] = NULL;
        if (run_as_user(tee, str_text(&content), content.len) != 0) rc = 1;
    } else {
        FILE *fp;
        if (mkdir_p(str_text(&dir)) != 0) rc = 1;
        fp = fopen(str_text(&path), "wb");
        if (fp == NULL) {
            rc = 1;
        } else {
            if (content.len > 0) fwrite(str_text(&content), 1, content.len, fp);
            fclose(fp);
        }
    }

    str_free(&content);
    str_free(&dir);
    str_free(&path);
    return rc;
}

static int usage(void) {
    fputs("usage: osr state <subcommand> [args]\n\n", stderr);
    fputs("  file                  path to the state file (no trailing newline)\n", stderr);
    fputs("  get <key>             the value, \"\" when unset\n", stderr);
    fputs("  set <key> <value>     write one key, preserving the others\n", stderr);
    fputs("  compose <key> <value> the whole new file, to stdout\n", stderr);
    return 2;
}

int osr_state_main(int argc, char **argv) {
    if (argc < 2) return usage();

    if (strcmp(argv[1], "file") == 0 && argc == 2) {
        Str path;
        str_init(&path);
        state_path(&path);
        out_flush(&path); /* printf '%s' -- no newline */
        str_free(&path);
        return 0;
    }
    if (strcmp(argv[1], "get") == 0 && argc == 3) return cmd_get(argv[2]);
    if (strcmp(argv[1], "set") == 0 && argc == 4) return cmd_set(argv[2], argv[3]);
    if (strcmp(argv[1], "compose") == 0 && argc == 4) return cmd_compose(argv[2], argv[3]);
    return usage();
}
