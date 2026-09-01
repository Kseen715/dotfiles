/* lib/gnome.c -- C port of lib/gnome.sh. See lib/gnome.h.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "module.h"

#include "cmds.h"
#include "fetch.h"
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
    if (p >= end) {
        /* No delimiter, so this is not a `<schema> <key> <value>` triple at
         * all -- it is a continuation of a listing gsettings wrapped, and it
         * carries a value with no key in front of it. There is no key here to
         * return, and returning the whole line would name the VALUE as the key
         * and issue `gsettings set <schema> "['<Super>r']" []`.
         *
         * That was harmless only by luck: gsettings rejects the unknown key,
         * and the caller ignores the status because a read-only key must not
         * end a run. lib/gnome.sh did the same thing, and the parity test that
         * compared them agreed with itself -- while its own comment said "a
         * line with no key name sets nothing", which is the behaviour meant
         * and now the behaviour implemented. */
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

/* --- shell extensions ----------------------------------------------------- */

/* shell_major -- "50" out of `gnome-shell --version`'s "GNOME Shell 50.1". The
 * extensions API keys its builds on the major and nothing else. */
static void shell_major(Str *out) {
    Str raw;
    char *argv[3];
    const char *p;

    str_init(&raw);
    argv[0] = (char *)"gnome-shell";
    argv[1] = (char *)"--version";
    argv[2] = NULL;
    (void)osr_run_capture(argv, &raw);
    p = str_text(&raw);
    while (*p != '\0' && (*p < '0' || *p > '9')) p++;
    while (*p >= '0' && *p <= '9') str_addc(out, *p++);
    str_free(&raw);
}

/* ext_data_home -- "XDG_DATA_HOME=<home>/.local/share", the environment
 * assignment every gnome-extensions call is made with.
 *
 * gnome-extensions unpacks into $XDG_DATA_HOME/gnome-shell/extensions and GNOME
 * Shell reads ~/.local/share/gnome-shell/extensions, and those are the same
 * directory only while XDG_DATA_HOME is unset or honest. A snap-confined
 * terminal is neither: VS Code's snap exports
 * XDG_DATA_HOME=$HOME/snap/code-insiders/<rev>/.local/share, so an install run
 * from that terminal unpacks the extension inside the snap, reports success,
 * and the Shell finds nothing at the next login. Nothing warns -- the
 * extension is installed, just not where anything looks.
 *
 * So the path is derived from the riced account's home (§8) rather than
 * inherited. That is also the right answer for `sudo osr`, where the ambient
 * XDG_DATA_HOME would be root's.
 */
static void ext_data_home(Str *out) {
    str_addz(out, "XDG_DATA_HOME=");
    str_addz(out, osr_mod_home());
    str_addz(out, "/.local/share");
}

/* ext_fetch -- the body of the step: ask the API for the build that matches
 * this Shell, download the zip, hand it to gnome-extensions. ctx is the UUID. */
static int ext_fetch(void *ctx) {
    const char *uuid = (const char *)ctx;
    Str major, json, url, zip, query, data_home;
    char *argv[7];
    int ok = 0;

    str_init(&major); str_init(&json); str_init(&url);
    str_init(&zip); str_init(&query); str_init(&data_home);
    shell_major(&major);
    ext_data_home(&data_home);

    str_addz(&query, "https://extensions.gnome.org/extension-info/?uuid=");
    str_addz(&query, uuid);
    str_addz(&query, "&shell_version=");
    str_addz(&query, str_text(&major));

    str_addz(&zip, env_str("TMPDIR", "/tmp"));
    str_addc(&zip, '/');
    str_addz(&zip, uuid);
    str_addz(&zip, ".zip");

    if (osr_fetch_buffer(&json, str_text(&query)) &&
        osr_json_string_field(&url, str_text(&json), "download_url") &&
        url.len > 0) {
        Str dl;
        str_init(&dl);
        str_addz(&dl, "https://extensions.gnome.org");
        str_addz(&dl, str_text(&url));
        if (osr_fetch_download(str_text(&dl), zip.p, 0)) {
            argv[0] = (char *)"env";
            argv[1] = (char *)str_text(&data_home);
            argv[2] = (char *)"gnome-extensions";
            argv[3] = (char *)"install";
            argv[4] = (char *)"--force";
            argv[5] = zip.p;
            argv[6] = NULL;
            ok = osr_run_user(argv) == 0;
        }
        str_free(&dl);
        (void)unlink(str_text(&zip));
    } else {
        osr_warnf("no build of %s for GNOME %s", uuid, str_text(&major));
    }

    str_free(&major); str_free(&json); str_free(&url);
    str_free(&zip); str_free(&query); str_free(&data_home);
    return ok;
}

/* ext_enable -- put the UUID in org.gnome.shell enabled-extensions, appending
 * to whatever is already there.
 *
 * NOT `gnome-extensions enable`: that verb asks the RUNNING Shell over D-Bus,
 * and the Shell knows nothing about an extension that was unpacked a second
 * ago -- it answers "Extension doesn't exist" and enables nothing. The zip
 * lands, the command fails, and the next login starts a Shell that has the
 * extension on disk and disabled. The key is the state the Shell reads at
 * startup, so writing it directly is what actually survives the logout the
 * user is about to do; a live Shell watches this key too and picks the
 * extension up once it has rescanned.
 *
 * Idempotent, and the list is shared state (SS5): every other extension in it
 * stays. The quoted UUID is what is searched for, so no uuid can match inside
 * another. */
static void ext_enable(const char *uuid) {
    Str existing, quoted, value;
    char *argv[6];

    str_init(&existing); str_init(&quoted); str_init(&value);
    str_addc(&quoted, '\'');
    str_addz(&quoted, uuid);
    str_addc(&quoted, '\'');

    argv[0] = (char *)"gsettings";
    argv[1] = (char *)"get";
    argv[2] = (char *)"org.gnome.shell";
    argv[3] = (char *)"enabled-extensions";
    argv[4] = NULL;
    (void)osr_run_user_capture(argv, &existing);
    str_trim_trailing(&existing, '\n');

    if (strstr(str_text(&existing), str_text(&quoted)) != NULL) {
        osr_infof("  %s already enabled", uuid);
        str_free(&existing); str_free(&quoted); str_free(&value);
        return;
    }

    /* Same append as osr_gnome_keybind's: a "'" in the value is the one
     * reliable sign there is a list element to keep, because gsettings spells
     * empty at least three ways ("@as []", "[]", "''"). */
    if (strchr(str_text(&existing), '\'') != NULL) {
        size_t len = existing.len;
        if (len > 0 && str_text(&existing)[len - 1] == ']') len--;
        str_add(&value, str_text(&existing), len);
        str_addz(&value, ", ");
        str_addz(&value, str_text(&quoted));
        str_addc(&value, ']');
    } else {
        str_addc(&value, '[');
        str_addz(&value, str_text(&quoted));
        str_addc(&value, ']');
    }

    argv[1] = (char *)"set";
    argv[4] = (char *)str_text(&value);
    argv[5] = NULL;
    if (osr_run_user(argv) != 0)
        osr_warnf("%s installed but not enabled - enable it in Extensions", uuid);
    str_free(&existing); str_free(&quoted); str_free(&value);
}

int osr_gnome_extension_install(const char *desc, const char *uuid) {

    /* No Shell, no extension host: an extension zip unpacked next to a session
     * that cannot load it is a directory nobody reads. */
    if (!osr_have_cmd("gnome-shell")) {
        osr_warnf("gnome-shell not found - skipping %s", uuid);
        return 0;
    }
    if (!osr_step_try(desc, ext_fetch, (void *)uuid)) return 0;

    ext_enable(uuid);
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
