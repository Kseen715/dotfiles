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
 * A KEY IS MATCHED LITERALLY. The sh original matched with sed and grep, i.e.
 * as basic regular expressions, so `osr_state_get "wallpaper.$OSR_THEME"`
 * really did treat that dot as "any character" -- and `wallpaper.nord` is a
 * real key, composed per theme by lib/config.c, sitting in a file that may
 * also hold `wallpaperXnord`. The regex reading of it was never wanted, it was
 * inherited; matching the bytes is what every caller means, it is what
 * test/unit_c/state_test.c asserts, and it costs this unit its one POSIX-only
 * dependency (<regex.h> does not exist off POSIX), which is why it is written
 * out here rather than compiled.
 *
 * The write is the one genuinely privileged step, and the only part of this
 * file with two bodies: see write_state below.
 *
 * C89 + POSIX, and C89 + Win32.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "common.h"
#include "cmds.h"

#ifndef _WIN32
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/* state_path -- "$OSR_HOME/.config/osr/state", where OSR_HOME is the account
 * being riced rather than whoever is running (osr_home). That indirection is
 * what makes the tests hermetic and what user-for-user installs point at, and
 * on Windows it is also what an elevated run needs: the state belongs in the
 * riced profile, not the admin's.
 *
 * `.config\\osr\\` on Windows too, deliberately: it is where this rice already
 * puts fastfetch's and wezterm's configs, so the state file is where someone
 * would look for it. */
static void state_path(Str *out) {
    str_addz(out, osr_home());
    str_addz(out, "/.config/osr/state");
}

/* key_regex -- compile "^<key>=" the way sed's `s/^KEY=//p` and grep's
 * `-v "^KEY="` read it: a BRE. Returns 0 when the key is not one. */
/* key_match -- does this line assign `key`? Returns the offset of the value
 * (just past the '='), or 0 when it does not -- 0 being impossible for a hit,
 * since a key is at least one byte. */
static size_t key_match(const Line *line, const char *key, size_t key_len) {
    if (line->len < key_len + 1) return 0;
    if (memcmp(line->start, key, key_len) != 0) return 0;
    if (line->start[key_len] != '=') return 0;
    return key_len + 1;
}

/* cmd_get -- sed -n "s/^KEY=//p" <file> | tail -n 1.
 *
 * Last assignment wins, matching how the file is rewritten. The trailing
 * newline is carried over from the matched line: sed does not add one to a
 * file that ended without it, and neither does tail.
 */
/* state_lookup -- the match itself: the value of the last `KEY=` line, and
 * whether that line ended in a newline (which `osr state get` prints back and
 * an in-process caller does not want). */
static int state_lookup(const char *key, Str *value, int *had_newline) {
    Str path;
    char *buf;
    size_t len;
    size_t pos = 0;
    size_t key_len = strlen(key);
    Line line;
    int found = 0;

    *had_newline = 0;
    str_init(&path);
    state_path(&path);
    buf = slurp(str_text(&path), &len);
    str_free(&path);
    if (buf == NULL) return 0;

    while (next_line(buf, len, &pos, &line)) {
        size_t at = key_match(&line, key, key_len);
        if (at == 0) continue;
        value->len = 0;
        str_add(value, line.start + at, line.len - at);
        found = 1;
        *had_newline = line.had_newline;
    }
    free(buf);
    return found;
}

/* osr_state_get -- the value a caller in this process wants: no trailing
 * newline, because every shell caller read it through `$( )`, which ate it. */
void osr_state_get(Str *out, const char *key) {
    int nl;
    str_reset(out);
    (void)state_lookup(key, out, &nl);
}

static int cmd_get(const char *key) {
    Str value;
    int nl = 0;

    str_init(&value);
    if (state_lookup(key, &value, &nl)) {
        Str out;
        str_init(&out);
        str_add(&out, str_text(&value), value.len);
        if (nl) str_addc(&out, '\n');
        out_flush(&out);
        str_free(&out);
    }
    str_free(&value);
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
    size_t key_len = strlen(key);
    Line line;
    Str body;

    str_init(&path);
    state_path(&path);
    buf = slurp(str_text(&path), &len);
    str_free(&path);

    str_init(&body);
    if (buf != NULL) {
        int first = 1;
        while (next_line(buf, len, &pos, &line)) {
            if (key_match(&line, key, key_len) != 0) continue;   /* this key's old value */
            if (!first) str_addc(&body, '\n');
            str_add(&body, line.start, line.len);
            first = 0;
        }
    }
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

/* write_state_plain -- the unprivileged write both bodies below end in:
 * create the directory, then replace the file. */
static int write_state_plain(const char *path, const char *dir, const Str *content) {
    FILE *fp;

    if (!osr_mkdir_parents(dir)) return 1;
    fp = fopen(path, "wb");
    if (fp == NULL) return 1;
    if (content->len > 0) fwrite(str_text(content), 1, content->len, fp);
    fclose(fp);
    return 0;
}

/* --- writing it ------------------------------------------------------------
 *
 * The state file belongs to the account being riced, not to whoever is
 * running the installer (section 8, user-for-user), and that is the whole
 * reason this has two bodies. On POSIX the installer may BE another account
 * -- `sudo ./osr install` runs as root -- so the write drops back to
 * $OSR_USER through `sudo -u`, which is what lib/state.sh's `as_user mkdir -p`
 * plus `as_user tee` did. On Windows there is no per-command identity to drop
 * to: an elevated run is a different PROCESS, and what it was told is which
 * profile to write into (osr_home, fed by --user-home across the elevation
 * boundary). So the path already points at the right account and the write is
 * a plain write.
 * ------------------------------------------------------------------------- */

#ifndef _WIN32

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

/* write_state -- the composed file into place, as $OSR_USER. */
static int write_state(const char *path, const char *dir, const Str *content) {
    int rc = 0;

    if (need_sudo()) {
        char *mk[4];
        char *tee[3];
        mk[0] = (char *)"mkdir"; mk[1] = (char *)"-p"; mk[2] = (char *)dir; mk[3] = NULL;
        if (run_as_user(mk, NULL, 0) != 0) rc = 1;
        tee[0] = (char *)"tee"; tee[1] = (char *)path; tee[2] = NULL;
        if (run_as_user(tee, str_text(content), content->len) != 0) rc = 1;
        return rc;
    }
    return write_state_plain(path, dir, content);
}

#else /* _WIN32 */

/* write_state -- a plain write: see this section's header on why there is no
 * identity to drop to here. */
static int write_state(const char *path, const char *dir, const Str *content) {
    return write_state_plain(path, dir, content);
}

#endif /* _WIN32 */

/* cmd_set -- osr_state_set: compose the new file, then write it as the user.
 * `mkdir -p` first, exactly as the sh version did. */
static int cmd_set(const char *key, const char *value) {
    Str path;
    Str content;
    char dir[OSR_PATH_MAX];
    int rc;

    str_init(&path);
    state_path(&path);
    osr_dirname(str_text(&path), dir, sizeof(dir));
    str_init(&content);
    compose(&content, key, value);

    rc = write_state(str_text(&path), dir, &content);

    str_free(&content);
    str_free(&path);
    return rc;
}

/* osr_state_set -- what `osr state set` does, without the fork. */
int osr_state_set(const char *key, const char *value) { return cmd_set(key, value) == 0; }

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
