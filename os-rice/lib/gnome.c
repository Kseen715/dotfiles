/* lib/gnome.c -- C port of lib/gnome.sh. See lib/gnome.h.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "module.h"

#include "cmds.h"
#include "gnome.h"

#define MEDIA_KEYS "org.gnome.settings-daemon.plugins.media-keys"
#define CUSTOM_KEY MEDIA_KEYS ".custom-keybinding"

/* The four schemas that can hold a chord upstream. list-recursively prints
 * "<schema> <key> <value>" per key, so field 2 is the key name. */
static const char *const KEY_SCHEMAS[] = {
    "org.gnome.shell.keybindings",
    "org.gnome.desktop.wm.keybindings",
    "org.gnome.mutter.keybindings",
    "org.gnome.mutter.wayland.keybindings",
    NULL
};

/* has_gnome -- a *GNOME* / *gnome* glob over one variable's value. */
static int has_gnome(const char *value) {
    const char *p;
    for (p = value; *p != '\0'; p++) {
        if ((*p == 'G' && strncmp(p, "GNOME", 5) == 0) ||
            (*p == 'g' && strncmp(p, "gnome", 5) == 0)) return 1;
    }
    return 0;
}

int osr_gnome_is_session(void) {
    return has_gnome(env_str("XDG_CURRENT_DESKTOP", "")) ||
           has_gnome(env_str("XDG_SESSION_DESKTOP", ""));
}

/* ci_contains -- a case-insensitive substring search over n bytes, which is
 * what `grep -iF` did to each line of the listing. */
static int ci_contains(const char *hay, size_t n, const char *needle) {
    size_t need = strlen(needle);
    size_t i;
    size_t j;

    if (need == 0) return 1;
    if (n < need) return 0;
    for (i = 0; i + need <= n; i++) {
        for (j = 0; j < need; j++) {
            char a = hay[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (j == need) return 1;
    }
    return 0;
}

/* field2 -- `cut -d' ' -f2` over one line: the text between the first and the
 * second space. A line with NO space is not skipped -- cut prints the whole
 * line when the delimiter is absent, and a wrapped listing line reaching the
 * key list is behaviour the shell tier has, so it is behaviour here too. */
static void field2(Str *out, const Line *line) {
    const char *p = line->start;
    const char *end = line->start + line->len;
    const char *start;

    while (p < end && *p != ' ') p++;
    if (p >= end) {                       /* no delimiter: the whole line */
        str_add(out, line->start, line->len);
        return;
    }
    p++;
    start = p;
    while (p < end && *p != ' ') p++;
    str_add(out, start, (size_t)(p - start));
}

int osr_gnome_free_binding(const char *binding) {
    Str quoted;
    int freed = 0;
    size_t i;

    /* The chord quoted on both sides, so "<Super>r" does not match the
     * "<Shift><Super>r" living in the same list. */
    str_init(&quoted);
    str_addc(&quoted, '\'');
    str_addz(&quoted, binding);
    str_addc(&quoted, '\'');

    for (i = 0; KEY_SCHEMAS[i] != NULL; i++) {
        Str listing;
        Str keys;
        size_t pos = 0;
        Line line;
        char *argv[6];
        const char *k;
        const char *kend;

        argv[0] = (char *)"gsettings";
        argv[1] = (char *)"list-recursively";
        argv[2] = (char *)KEY_SCHEMAS[i];
        argv[3] = NULL;
        str_init(&listing);
        (void)osr_run_user_capture(argv, &listing);

        str_init(&keys);
        while (next_line(str_text(&listing), listing.len, &pos, &line)) {
            if (!ci_contains(line.start, line.len, str_text(&quoted))) continue;
            field2(&keys, &line);
            str_addc(&keys, '\n');
        }

        /* `for _gfb_key in $_gfb_keys` -- unquoted, so the split is on
         * whitespace across the whole collected list, not per line, and an
         * empty result iterates zero times. */
        k = str_text(&keys);
        kend = k + keys.len;
        while (k < kend) {
            const char *start;
            Str key;
            while (k < kend && is_space(*k)) k++;
            if (k >= kend) break;
            start = k;
            while (k < kend && !is_space(*k)) k++;
            str_init(&key);
            str_add(&key, start, (size_t)(k - start));

            argv[0] = (char *)"gsettings";
            argv[1] = (char *)"set";
            argv[2] = (char *)KEY_SCHEMAS[i];
            argv[3] = (char *)str_text(&key);
            argv[4] = (char *)"[]";
            argv[5] = NULL;
            /* Best effort: a read-only key must not end the run. */
            (void)osr_run_user_quiet(argv);
            osr_infof("  freed %s from %s.%s", binding, KEY_SCHEMAS[i],
                      str_text(&key));
            freed = 1;
            str_free(&key);
        }
        str_free(&keys);
        str_free(&listing);
    }

    if (!freed)
        osr_infof("  %s was not bound to a known GNOME Shell key -- nothing to unbind",
                  binding);
    str_free(&quoted);
    return 1;
}

int osr_gnome_keybind(const char *id, const char *name, const char *binding,
                      const char *command) {
    Str path;
    Str child;
    Str existing;
    char *argv[6];

    str_init(&path);
    str_addz(&path, "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/");
    str_addz(&path, id);
    str_addc(&path, '/');

    str_init(&child);
    str_addz(&child, CUSTOM_KEY);
    str_addc(&child, ':');
    str_addz(&child, str_text(&path));

    str_init(&existing);
    argv[0] = (char *)"gsettings";
    argv[1] = (char *)"get";
    argv[2] = (char *)MEDIA_KEYS;
    argv[3] = (char *)"custom-keybindings";
    argv[4] = NULL;
    (void)osr_run_user_capture(argv, &existing);
    /* `echo "$x" | grep -q` saw the value with its trailing newline; the
     * search itself does not care, but the append below would. */
    str_trim_trailing(&existing, '\n');

    if (strstr(str_text(&existing), str_text(&path)) != NULL) {
        osr_infof("  %s %s shortcut already registered", id, binding);
        str_free(&path); str_free(&child); str_free(&existing);
        return 1;
    }

    argv[0] = (char *)"gsettings";
    argv[1] = (char *)"set";
    argv[2] = (char *)str_text(&child);
    argv[3] = (char *)"name";
    argv[4] = (char *)name;
    argv[5] = NULL;
    (void)osr_run_user(argv);
    argv[3] = (char *)"binding";
    argv[4] = (char *)binding;
    (void)osr_run_user(argv);
    argv[3] = (char *)"command";
    argv[4] = (char *)command;
    (void)osr_run_user(argv);

    /* Append to the parent list. "Already has an entry" is decided by looking
     * for a path separator rather than by matching the empty forms: gsettings
     * spells empty at least three ways depending on version and on whether the
     * key was ever written ("@as []", "[]", "''"), and appending to one of
     * those produces a list whose first element is garbage. Every real element
     * is a dconf path, so a "/" in the value is the one reliable sign there is
     * something to keep. */
    {
        Str value;
        str_init(&value);
        if (strchr(str_text(&existing), '/') != NULL) {
            /* "${_gk_existing%]}, '<path>']" -- the LAST ] dropped, and only
             * when it is the final character. */
            size_t len = existing.len;
            if (len > 0 && str_text(&existing)[len - 1] == ']') len--;
            str_add(&value, str_text(&existing), len);
            str_addz(&value, ", '");
        } else {
            str_addz(&value, "['");
        }
        str_addz(&value, str_text(&path));
        str_addz(&value, "']");

        argv[2] = (char *)MEDIA_KEYS;
        argv[3] = (char *)"custom-keybindings";
        argv[4] = (char *)str_text(&value);
        (void)osr_run_user(argv);
        str_free(&value);
    }

    osr_infof("  %s %s shortcut registered at %s", id, binding, str_text(&path));
    str_free(&path); str_free(&child); str_free(&existing);
    return 1;
}

static int gnome_usage(void) {
    fputs("usage: osr gnome <subcommand> [args]\n\n", stderr);
    fputs("  is-session                     exit 0 in a GNOME session\n", stderr);
    fputs("  free-binding <chord>           unbind it from every GNOME Shell key\n", stderr);
    fputs("  keybind <id> <name> <chord> <command>   register a custom shortcut\n", stderr);
    return 2;
}

int osr_gnome_main(int argc, char **argv) {
    if (argc < 2) return gnome_usage();

    if (strcmp(argv[1], "is-session") == 0 && argc == 2)
        return osr_gnome_is_session() ? 0 : 1;
    if (strcmp(argv[1], "free-binding") == 0 && argc == 3)
        return osr_gnome_free_binding(argv[2]) ? 0 : 1;
    if (strcmp(argv[1], "keybind") == 0 && argc == 6)
        return osr_gnome_keybind(argv[2], argv[3], argv[4], argv[5]) ? 0 : 1;

    return gnome_usage();
}
