/* modules/evolution.c -- Evolution mail/calendar/contacts, made to look like a
 * 2020s client instead of a 2009 one.
 *
 * Evolution's appearance comes from three places, and only one of them is a
 * config file, which is why this module is longer than "install a package":
 *
 *   1. GSettings      layout, density, whether HTML mail is allowed to paint its
 *                     own white background over your dark theme
 *   2. a scoped GTK   Evolution is GTK3 and reads the global gtk.css, so the
 *      theme          rice's tweaks are shipped as a *private theme* the .desktop
 *                     override selects with GTK_THEME. That keeps the rounded
 *                     header-bar look off every other GTK app.
 *   3. fonts          the message body is WebKit; it uses its own font settings
 *                     unless you tell it to follow the desktop
 *
 * GSettings keys come and go between Evolution releases, so each one is applied
 * only if the installed schema actually has it (`gsettings list-keys`). A key
 * this version does not know is a skipped line, not a failed module (§9).
 *
 * evolution-ews is the Exchange/Office365 backend -- the single most common
 * reason an account cannot be added at all, and it is a separate package
 * everywhere.
 *
 * The two support packages are what the GSettings half runs on:
 *   dconf   the GSettings *backend*. Without it GSettings falls back to the
 *           memory backend and every `gsettings set` is discarded at logout.
 *   glib2   ships the `gsettings` binary itself. All the per-distro spellings
 *           are absorbed by pkgmap rows (§1), so this list stays one list.
 *
 * Was modules/evolution.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

/* trim_line -- the sh module's sed: leading blanks off, a trailing comment off,
 * trailing blanks off. Comment handling has to survive hex colors: in
 * `citation-color '#d98cae'` the # is a value, not a comment. So: trim, then
 * drop only a comment that is PRECEDED by whitespace (the same rule
 * _pkgmap_one uses), then skip whole-line comments by their first character. */
static void trim_line(Str *out, const char *start, size_t len) {
    size_t i = 0, end;

    while (i < len && is_space(start[i])) i++;
    end = len;
    {
        size_t j;
        for (j = i; j + 1 < len; j++) {
            if (is_space(start[j]) && start[j + 1] == '#') { end = j; break; }
        }
    }
    while (end > i && is_space(start[end - 1])) end--;
    str_add(out, start + i, end - i);
}

/* after_word -- ${line#"<word> "}: the remainder past that word and its one
 * separating space, or the whole string when it is not followed by one. */
static const char *after_word(const char *line, const char *word) {
    size_t n = strlen(word);
    if (strncmp(line, word, n) == 0 && line[n] == ' ') return line + n + 1;
    return line;
}

static void first_word(Str *out, const char *s) {
    size_t i = 0;
    while (s[i] != '\0' && s[i] != ' ') i++;
    str_add(out, s, i);
}

/* has_key -- `as_user gsettings list-keys <schema> | grep -qx <key>`. */
static int has_key(const char *schema, const char *key) {
    Str out;
    char *argv[4];
    size_t pos = 0;
    Line line;
    size_t klen = strlen(key);
    int found = 0;

    str_init(&out);
    argv[0] = (char *)"gsettings"; argv[1] = (char *)"list-keys";
    argv[2] = (char *)schema; argv[3] = NULL;
    if (osr_run_user_capture(argv, &out)) {
        while (!found && next_line(str_text(&out), out.len, &pos, &line))
            if (line.len == klen && strncmp(line.start, key, klen) == 0) found = 1;
    }
    str_free(&out);
    return found;
}

/* gsettings_apply -- apply `<schema> <key> <value>` lines as OSR_USER, skipping
 * keys the installed version does not have. Comments and blanks ignored. */
static void gsettings_apply(const char *file) {
    char *buf;
    size_t len;
    size_t pos = 0;
    Line raw;
    Str base;
    long set = 0, skip = 0;

    buf = slurp(file, &len);
    if (buf == NULL) return;

    str_init(&base);
    base_of(&base, file);
    if (!osr_have_cmd("gsettings")) {
        osr_warnf("gsettings not available - skipping %s", str_text(&base));
        str_free(&base);
        free(buf);
        return;
    }

    while (next_line(buf, len, &pos, &raw)) {
        Str line, schema, key;
        const char *rest, *val;
        char *argv[6];

        str_init(&line);
        trim_line(&line, raw.start, raw.len);
        if (line.len == 0 || str_text(&line)[0] == '#') { str_free(&line); continue; }

        str_init(&schema); str_init(&key);
        first_word(&schema, str_text(&line));
        rest = after_word(str_text(&line), str_text(&schema));
        first_word(&key, rest);
        val = after_word(rest, str_text(&key));

        if (schema.len == 0 || key.len == 0 || *val == '\0') {
            str_free(&line); str_free(&schema); str_free(&key);
            continue;
        }
        if (has_key(str_text(&schema), str_text(&key))) {
            argv[0] = (char *)"gsettings"; argv[1] = (char *)"set";
            argv[2] = schema.p; argv[3] = key.p; argv[4] = (char *)val; argv[5] = NULL;
            if (osr_run_user_quiet(argv) == 0) set++;
            else osr_warnf("gsettings set %s %s '%s' failed",
                           str_text(&schema), str_text(&key), val);
        } else {
            skip++;
        }
        str_free(&line); str_free(&schema); str_free(&key);
    }
    osr_infof("%s: %ld key(s) applied, %ld not present in this version",
              str_text(&base), set, skip);
    str_free(&base);
    free(buf);
}

/* prefix_exec -- `sed 's|^Exec=|Exec=env GTK_THEME=osr-evolution |'`: leaves the
 * rest of the entry (icon, MIME types, actions) exactly as packaged. */
static void prefix_exec(Str *out, const char *text, size_t len) {
    size_t pos = 0;
    Line line;

    while (next_line(text, len, &pos, &line)) {
        if (line.len >= 5 && strncmp(line.start, "Exec=", 5) == 0) {
            str_addz(out, "Exec=env GTK_THEME=osr-evolution ");
            str_add(out, line.start + 5, line.len - 5);
        } else {
            str_add(out, line.start, line.len);
        }
        str_addc(out, '\n');
    }
}

int osrm_evolution(void) {
    static const char *const pkgs[] = {
        "evolution", "evolution-data-server", "evolution-ews",
        "dconf", "glib2", "gsettings-desktop-schemas", NULL
    };
    /* Both names the packages ship. */
    static const char *const launchers[] = {
        "/usr/share/applications/org.gnome.Evolution.desktop",
        "/usr/share/applications/evolution.desktop",
        NULL
    };
    Str path, theme_dir, dst;
    size_t i;
    int ok;

    ok = osr_pkg_install_step("Installing Evolution", pkgs);

    /* --- 1. GSettings ---------------------------------------------------- */
    str_init(&path);
    str_addz(&path, osr_mod_dotfiles()); str_addz(&path, "/evolution/gsettings.conf");
    gsettings_apply(str_text(&path));           /* behaviour, dotfiles-owned */
    if (*osr_mod_theme_dir() != '\0') {
        str_reset(&path);
        str_addz(&path, osr_mod_theme_dir());
        str_addz(&path, "/config/evolution/gsettings.conf");
        gsettings_apply(str_text(&path));       /* appearance, rice-owned (§6) */
    }

    /* --- 2. the scoped GTK theme ------------------------------------------
     * A private theme rather than an addition to ~/.config/gtk-3.0/gtk.css:
     * GTK3 has no per-application CSS selector, so anything written there would
     * restyle every GTK app on the machine. The theme imports Adwaita-dark from
     * GTK's own resource bundle and only overrides on top of it. */
    str_init(&theme_dir); str_init(&dst);
    str_addz(&theme_dir, osr_mod_home());
    str_addz(&theme_dir, "/.local/share/themes/osr-evolution/gtk-3.0");
    str_addz(&dst, str_text(&theme_dir)); str_addz(&dst, "/gtk.css");
    if (osr_mkdir_p(str_text(&theme_dir))
        && osr_install_theme_layer("evolution", "gtk.css", str_text(&dst))) {
        /* .desktop override that selects it. A user-level copy in
         * ~/.local/share/applications wins over the packaged one without
         * touching /usr, and `update-desktop-database` is what makes the menu
         * notice. */
        Str apps;
        int done = 0;

        str_init(&apps);
        str_addz(&apps, osr_mod_home()); str_addz(&apps, "/.local/share/applications");
        ok = osr_mkdir_p(str_text(&apps)) && ok;

        for (i = 0; launchers[i] != NULL && !done; i++) {
            Str base, body;
            char *entry;
            size_t elen;

            if (!file_exists(launchers[i])) continue;
            entry = slurp(launchers[i], &elen);
            if (entry == NULL) continue;

            str_init(&base); str_init(&body);
            base_of(&base, launchers[i]);
            osr_infof("installing themed launcher: %s", str_text(&base));
            prefix_exec(&body, entry, elen);
            free(entry);

            str_reset(&dst);
            str_addz(&dst, str_text(&apps)); str_addc(&dst, '/');
            str_addz(&dst, str_text(&base));
            ok = osr_write_user(str_text(&dst), str_text(&body)) && ok;
            done = 1;
            str_free(&base); str_free(&body);
        }
        /* The theme is installed either way, but without the launcher nothing
         * selects it -- say so instead of leaving a theme dir nobody reads. */
        if (!done)
            osr_warn("no Evolution .desktop in /usr/share/applications - theme "
                     "installed but not selected (rerun this module after "
                     "installing evolution)");
        if (osr_have_cmd("update-desktop-database")) {
            char *argv[3];
            argv[0] = (char *)"update-desktop-database"; argv[1] = apps.p; argv[2] = NULL;
            (void)osr_run_user_quiet(argv);
        }
        str_free(&apps);
    }

    /* --- 3. mail defaults --------------------------------------------------
     * Evolution registers itself as a mailto: handler only once it has run.
     * Claim it now so a link in the browser opens the client the rice actually
     * installed. */
    if (osr_have_cmd("xdg-mime")) {
        char *argv[5];
        argv[0] = (char *)"xdg-mime"; argv[1] = (char *)"default";
        argv[2] = (char *)"org.gnome.Evolution.desktop";
        argv[3] = (char *)"x-scheme-handler/mailto"; argv[4] = NULL;
        (void)osr_run_user_quiet(argv);
    }

    str_free(&path); str_free(&theme_dir); str_free(&dst);
    return ok;
}
