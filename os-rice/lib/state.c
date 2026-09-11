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
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "common.h"
#include "cmds.h"

#include "../thirdparty/yaml.h"

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
    str_addzz(out, osr_home(), "/.config/osr/state.yaml", (const char *)NULL);
}

/* legacy_path -- the flat `key=value` file this one replaced. Read when the
 * YAML is not there yet and deleted by the first write, so a machine riced
 * before the change keeps its rice, theme and wallpaper across the upgrade
 * instead of looking like it was never riced. */
static void legacy_path(Str *out) {
    str_addzz(out, osr_home(), "/.config/osr/state", (const char *)NULL);
}

/* key_match -- does this line assign `key`? Returns the offset of the value
 * (just past the '='), or 0 when it does not -- 0 being impossible for a hit,
 * since a key is at least one byte. The `key=value` shape is no longer the
 * FILE's shape, it is the in-memory one: load() flattens the YAML mapping into
 * it, and everything downstream reads and rewrites that. Keeping the internal
 * representation is what makes this a format change and not a rewrite. */
static size_t key_match(const Line *line, const char *key, size_t key_len) {
    if (line->len < key_len + 1) return 0;
    if (memcmp(line->start, key, key_len) != 0) return 0;
    if (line->start[key_len] != '=') return 0;
    return key_len + 1;
}

/* load_legacy -- the pre-YAML file, already in the internal shape. */
static int load_legacy(Str *kv) {
    Str path;
    char *buf;
    size_t len;

    str_init(&path);
    legacy_path(&path);
    buf = slurp(str_text(&path), &len);
    str_free(&path);
    if (buf == NULL) return 0;
    str_add(kv, buf, len);
    if (kv->len > 0 && str_text(kv)[kv->len - 1] != '\n') str_addc(kv, '\n');
    free(buf);
    return 1;
}

/* load -- the state file, as the internal `key=value` lines plus the `modules`
 * sequence (one name per line). Either may be NULL when a caller wants only
 * the other.
 *
 * Read through the vendored parser (thirdparty/yaml.h) rather than a `while
 * read` loop, because this file is in the user's home and someone will edit
 * it: flow style, quoted scalars, comments and a document marker are all
 * things a hand edit leaves behind, and all of them are what a parser is for.
 * Only the ROOT mapping's scalar entries are state -- a nested structure
 * someone adds is skipped rather than flattened into a bogus key. */
static void load(Str *kv, Str *mods) {
    yaml_parser_t parser;
    yaml_event_t ev;
    Str path, key;
    FILE *f;
    int depth = 0;       /* nesting inside the document */
    int want_key = 0;    /* the next root-level scalar names a key */
    int in_mods = 0;
    int done = 0;

    str_init(&path);
    state_path(&path);
    f = fopen(str_text(&path), "rb");
    str_free(&path);
    if (f == NULL) { if (kv != NULL) (void)load_legacy(kv); return; }

    if (!yaml_parser_initialize(&parser)) { fclose(f); return; }
    yaml_parser_set_input_file(&parser, f);
    str_init(&key);

    while (!done && yaml_parser_parse(&parser, &ev)) {
        switch (ev.type) {
        case YAML_MAPPING_START_EVENT:
            depth++;
            want_key = (depth == 1);
            break;
        case YAML_MAPPING_END_EVENT:
            depth--;
            want_key = (depth == 1);
            break;
        case YAML_SEQUENCE_START_EVENT:
            if (depth == 1 && !want_key && key.len == 7
                && memcmp(str_text(&key), "modules", 7) == 0) in_mods = 1;
            depth++;
            break;
        case YAML_SEQUENCE_END_EVENT:
            depth--;
            if (depth == 1) { in_mods = 0; want_key = 1; }
            break;
        case YAML_SCALAR_EVENT: {
            const char *v = (const char *)ev.data.scalar.value;
            size_t n = ev.data.scalar.length;
            if (in_mods && depth == 2) {
                if (mods != NULL && n > 0) { str_add(mods, v, n); str_addc(mods, '\n'); }
            } else if (depth == 1 && want_key) {
                str_reset(&key);
                str_add(&key, v, n);
                want_key = 0;
            } else if (depth == 1) {
                /* A value. A newline inside one would break the internal line
                 * shape, so it ends the value -- nothing this writes has one,
                 * and a hand edit that introduces one loses the rest of it
                 * rather than corrupting the next key. */
                if (kv != NULL && key.len > 0) {
                    const char *nl = (const char *)memchr(v, '\n', n);
                    if (nl != NULL) n = (size_t)(nl - v);
                    str_add(kv, str_text(&key), key.len);
                    str_addc(kv, '=');
                    str_add(kv, v, n);
                    str_addc(kv, '\n');
                }
                want_key = 1;
            }
            break;
        }
        case YAML_STREAM_END_EVENT:
            done = 1;
            break;
        default:
            break;
        }
        yaml_event_delete(&ev);
    }
    str_free(&key);
    yaml_parser_delete(&parser);
    fclose(f);
}

/* emit -- the whole file, from the internal pair.
 *
 * Written as plain text while the read side is a parser, and the asymmetry is
 * deliberate: what this program writes is a flat mapping of short scalars, and
 * every value goes out double-quoted, which has exactly one escaping rule (a
 * backslash before a backslash or a quote) and no block/flow/indent decision
 * to get wrong. What it READS may be anything a person typed.
 *
 * Comments in a hand-edited file do not survive a rewrite. libyaml's parser
 * does not report them, so there is nothing to carry over; the header below is
 * put back on every write so the file always says what it is. */
static void emit(Str *out, const Str *kv, const Str *mods) {
    size_t pos = 0;
    Line l;

    str_addz(out,
        "# os-rice state -- what is applied to this machine, and what is\n"
        "# installed on it. Written by `osr install`, `osr theme` and\n"
        "# `osr module <name>`; read by all three.\n"
        "#\n"
        "# `modules` is the set a theme apply repaints: a rice manifest says\n"
        "# what a rice SHIPS, this says what is really here. Safe to hand-edit\n"
        "# -- drop a line to stop repainting that app, add one to start.\n");

    while (next_line(str_text(kv), kv->len, &pos, &l)) {
        size_t i;
        size_t at = 0;
        if (l.len == 0) continue;
        while (at < l.len && l.start[at] != '=') at++;
        if (at == 0 || at == l.len) continue;   /* no key, or no '=' */
        str_add(out, l.start, at);
        str_addz(out, ": \"");
        for (i = at + 1; i < l.len; i++) {
            if (l.start[i] == '\\' || l.start[i] == '"') str_addc(out, '\\');
            str_addc(out, l.start[i]);
        }
        str_addz(out, "\"\n");
    }

    if (mods == NULL || mods->len == 0) return;
    str_addz(out, "modules:\n");
    pos = 0;
    while (next_line(str_text(mods), mods->len, &pos, &l)) {
        if (l.len == 0) continue;
        str_addz(out, "  - ");
        str_add(out, l.start, l.len);
        str_addc(out, '\n');
    }
}

/* state_lookup -- the value of the last `key` entry, and whether the file's
 * matched line ended in a newline (which `osr state get` prints back and an
 * in-process caller does not want). Every loaded line is newline-terminated,
 * so the flag is now only ever false for a value read out of a legacy file
 * that ended without one. */
static int state_lookup(const char *key, Str *value, int *had_newline) {
    Str kv;
    size_t pos = 0;
    size_t key_len = strlen(key);
    Line line;
    int found = 0;

    *had_newline = 0;
    str_init(&kv);
    load(&kv, NULL);

    while (next_line(str_text(&kv), kv.len, &pos, &line)) {
        size_t at = key_match(&line, key, key_len);
        if (at == 0) continue;
        value->len = 0;
        str_add(value, line.start + at, line.len - at);
        found = 1;
        *had_newline = line.had_newline;
    }
    str_free(&kv);
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

/* compose -- the whole new file for one key set to one value: every OTHER key
 * as it stands, then this one, then the modules list untouched. The key moves
 * to the end when it is rewritten, which is what the flat file did and what
 * makes the file record the order things were last written in. */
static void compose(Str *out, const char *key, const char *value) {
    Str kv, mods, body;
    size_t pos = 0;
    size_t key_len = strlen(key);
    Line line;

    str_initv(&kv, &mods, &body, (Str *)NULL);
    load(&kv, &mods);

    while (next_line(str_text(&kv), kv.len, &pos, &line)) {
        if (line.len == 0) continue;
        if (key_match(&line, key, key_len) != 0) continue;   /* this key's old value */
        str_add(&body, line.start, line.len);
        str_addc(&body, '\n');
    }
    str_addzz(&body, key, "=", value, "\n", (const char *)NULL);

    emit(out, &body, &mods);
    str_freev(&body, &mods, &kv, (Str *)NULL);
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

/* remove_as_user -- delete a file in the user's home, as them. Same reason the
 * write is escalated: root unlinking something under $HOME is a privilege this
 * does not need. A failure is ignored -- the file it removes is superseded,
 * not load-bearing. */
static void remove_as_user(const char *path) {
    if (need_sudo()) {
        char *rm[4];
        rm[0] = (char *)"rm"; rm[1] = (char *)"-f"; rm[2] = (char *)path; rm[3] = NULL;
        (void)run_as_user(rm, NULL, 0);
        return;
    }
    (void)remove(path);
}

#else /* _WIN32 */

/* write_state -- a plain write: see this section's header on why there is no
 * identity to drop to here. */
static int write_state(const char *path, const char *dir, const Str *content) {
    return write_state_plain(path, dir, content);
}

static void remove_as_user(const char *path) { (void)remove(path); }

#endif /* _WIN32 */

/* write_state_file -- the composed file, written as the user, and the flat
 * file it replaced removed in the same breath. Removing it is what keeps the
 * upgrade one-way: leaving a readable `state` beside `state.yaml` would give
 * an older binary -- or a script someone wrote against it -- a second, stale
 * answer to "what rice is this". It is gone only once the new file is
 * actually on disk. */
static int write_state_file(const Str *content) {
    Str path;
    char dir[OSR_PATH_MAX];
    int rc;

    str_init(&path);
    state_path(&path);
    osr_dirname(str_text(&path), dir, sizeof(dir));
    rc = write_state(str_text(&path), dir, content);
    str_free(&path);

    if (rc == 0) {
        Str old;
        str_init(&old);
        legacy_path(&old);
        if (file_exists(str_text(&old))) remove_as_user(str_text(&old));
        str_free(&old);
    }
    return rc;
}

/* cmd_set -- osr_state_set: compose the new file, then write it as the user.
 * `mkdir -p` first, exactly as the sh version did. */
static int cmd_set(const char *key, const char *value) {
    Str content;
    int rc;

    str_init(&content);
    compose(&content, key, value);
    rc = write_state_file(&content);
    str_free(&content);
    return rc;
}

/* osr_state_set -- what `osr state set` does, without the fork. */
int osr_state_set(const char *key, const char *value) { return cmd_set(key, value) == 0; }

/* --- the installed-module record ---------------------------------------------
 *
 * The `modules` sequence of the same file: the modules this machine has
 * actually had installed. It answers the one question the scalars above
 * cannot -- what is really here. A rice manifest is what a rice SHIPS, and a
 * theme apply that repaints only that leaves anything added later with
 * `osr module <name>` in its old colors through every switch (lib/apply.c
 * reads this).
 */
void osr_installed_get(Str *out) { load(NULL, out); }

int osr_installed_add(const char *name) {
    Str kv, mods, content;
    size_t pos = 0;
    size_t nlen;
    Line l;
    int rc;

    if (name == NULL || name[0] == '\0') return 1;
    nlen = strlen(name);

    str_initv(&kv, &mods, &content, (Str *)NULL);
    load(&kv, &mods);
    while (next_line(str_text(&mods), mods.len, &pos, &l)) {
        if (l.len == nlen && memcmp(l.start, name, nlen) == 0) {
            str_freev(&kv, &mods, &content, (Str *)NULL);
            return 1;   /* already recorded -- adding twice is a no-op */
        }
    }
    str_addzz(&mods, name, "\n", (const char *)NULL);

    emit(&content, &kv, &mods);
    rc = write_state_file(&content);
    str_freev(&kv, &mods, &content, (Str *)NULL);
    return rc == 0;
}

static int usage(void) {
    fputs("usage: osr state <subcommand> [args]\n\n", stderr);
    fputs("  file                  path to the state file (no trailing newline)\n", stderr);
    fputs("  get <key>             the value, \"\" when unset\n", stderr);
    fputs("  set <key> <value>     write one key, preserving the others\n", stderr);
    fputs("  compose <key> <value> the whole new file, to stdout\n", stderr);
    fputs("  installed             the modules recorded as installed, one per line\n", stderr);
    fputs("  installed add <name>  record one (idempotent)\n", stderr);
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
    if (strcmp(argv[1], "installed") == 0 && argc == 2) {
        Str list;
        str_init(&list);
        osr_installed_get(&list);
        out_flush(&list);
        str_free(&list);
        return 0;
    }
    if (strcmp(argv[1], "installed") == 0 && argc == 4 && strcmp(argv[2], "add") == 0)
        return osr_installed_add(argv[3]) ? 0 : 1;
    return usage();
}
