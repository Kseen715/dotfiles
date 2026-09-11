/* modules/theming.c -- toolkit theming for a WM that has no settings daemon
 * (i3-sugg §4). Under GNOME/KDE something applies your theme; under i3 nothing
 * does, which is why "the theme only works in some apps" is the single most
 * common i3 complaint.
 *
 * Four consumers, four mechanisms, one rice:
 *
 *   GTK2   ~/.gtkrc-2.0
 *   GTK3   ~/.config/gtk-3.0/settings.ini (+ gtk.css for accents)
 *   GTK4   ~/.config/gtk-4.0/ + gsettings
 *   Qt5/6  qt5ct/qt6ct, both selected by QT_QPA_PLATFORMTHEME=qt5ct (the qt6ct
 *          plugin registers that key too), exported from the xprofile layer for
 *          X11 and from ~/.config/environment.d for a Wayland/systemd session
 *
 * xsettingsd is the daemon that pushes theme/font/DPI to already-running GTK2/3
 * apps (live-reload with `killall -HUP xsettingsd`). Everything below except the
 * packages is rice-owned and swaps on a rice switch (§6).
 *
 * Was modules/theming.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/gnome.h"
#include "../lib/common.h"
#include "../lib/cmds.h"
#include "../lib/config.h"
#include "../lib/fonts.h"
#include "../lib/render.h"

#include <stddef.h>
#include <string.h>
#include <unistd.h>

static int nerd_font(void *ctx) { return osr_install_nerd_font((const char *)ctx); }

/* gset -- `as_user gsettings set org.gnome.desktop.interface <key> <value>`,
 * then read the key back and say so when it did not take.
 *
 * Still best-effort -- there is no dconf daemon in a container, and a desktop
 * that does not read these keys is not a failed rice (§9) -- but silent was
 * too quiet. `gsettings set` exits 0 on a write dconf then drops (a session
 * bus the caller cannot reach, a schema shadowed by a snap's
 * GSETTINGS_SCHEMA_DIR), so the exit status alone proves nothing and the
 * desktop just kept its old colours with no line anywhere saying why. The
 * read-back is the only honest check. */
static void gset(const char *key, const char *value) {
    char *argv[6];
    Str got;
    const char *text;
    size_t len;

    argv[0] = (char *)osr_gsettings(); argv[1] = (char *)"set";
    argv[2] = (char *)"org.gnome.desktop.interface";
    argv[3] = (char *)key; argv[4] = (char *)value; argv[5] = NULL;
    if (osr_run_user_quiet(argv) != 0) {
        osr_warnf("gsettings could not set %s=%s", key, value);
        return;
    }

    str_init(&got);
    argv[1] = (char *)"get"; argv[4] = NULL;
    if (osr_run_user_capture(argv, &got) != 0) { str_free(&got); return; }

    /* `gsettings get` quotes a string ('purple') and does not quote the rest;
     * trim the quotes and the newline before comparing. */
    text = str_text(&got);
    len = got.len;
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) len--;
    if (len >= 2 && text[0] == '\'' && text[len - 1] == '\'') { text++; len -= 2; }
    if (strlen(value) != len || strncmp(text, value, len) != 0)
        osr_warnf("%s did not take - it reads %.*s, not %s "
                  "(no dconf for this session?)", key, (int)len, text, value);
    str_free(&got);
}

/* theme_present -- is a theme of that name unpacked on this box? `sub` is the
 * XDG subdirectory the toolkit looks in: "themes" for a GTK theme, "icons" for
 * an icon or cursor set. The four roots are the ones GTK itself searches, most
 * specific first. */
static int theme_present(const char *sub, const char *name) {
    static const char *const roots[] = {
        "$HOME/.local/share/", "$HOME/.", "/usr/local/share/", "/usr/share/", NULL
    };
    Str path;
    size_t i;
    int there = 0;

    str_init(&path);
    for (i = 0; roots[i] != NULL && !there; i++) {
        str_reset(&path);
        if (strncmp(roots[i], "$HOME", 5) == 0)
            str_addzz(&path, osr_mod_home(), roots[i] + 5, sub, "/", name,
                      (const char *)NULL);
        else
            str_addzz(&path, roots[i], sub, "/", name, (const char *)NULL);
        there = dir_exists(str_text(&path));
    }
    str_free(&path);
    return there;
}

/* want_theme -- make sure the theme the theme.list asks for is actually here.
 *
 * gsettings and settings.ini take a NAME, and a name nothing provides is not
 * an error anywhere: GTK quietly falls back to Adwaita and the rice looks
 * half-applied with no line saying why. So install it when it is missing and
 * we are allowed to install (a theme-only pass has no sudo and no business
 * touching packages), and say so when it is still missing after that. The
 * package is looked up under the theme's own name, which is what the pkgmap
 * rows at the end of any.map are for; a name with no row and no package fails
 * one non-fatal step and lands on the same warning. */
static void want_theme(const char *sub, const char *name, const char *what) {
    const char *pkg[2];
    Str desc;

    if (*name == '\0' || theme_present(sub, name)) return;

    if (!osr_theme_only()) {
        str_init(&desc);
        str_addzz(&desc, "Installing the ", what, " ", name, (const char *)NULL);
        pkg[0] = name; pkg[1] = NULL;
        (void)osr_pkg_install_step_try(str_text(&desc), pkg);
        str_free(&desc);
        if (theme_present(sub, name)) return;
    }
    osr_warnf("%s '%s' is not installed - apps fall back to the default",
              what, name);
}

int osrm_theming(void) {
    static const char *const stack[] = {
        "xsettingsd", "lxappearance", "qt5ct", "qt6ct", "kvantum",
        "gtk-dark-theme", "papirus-icon-theme", "adwaita-icon-theme",
        "xcursor-themes", NULL
    };
    static const char *const fonts[] = {
        "fontconfig", "noto-fonts", "noto-fonts-emoji", "noto-fonts-cjk",
        "ttf-liberation", "ttf-dejavu", "nerd-fonts-symbols", NULL
    };
    /* One template per FILE SHAPE, not per theme (§6b): settings.ini is
     * rendered twice because GTK3 and GTK4 read the same keys from two paths,
     * and the Qt color scheme is rendered twice for the same reason. What used
     * to be ten per-theme files is now six templates plus the theme's palette.
     * { app, file, path under $HOME } */
    static const char *const layers[] = {
        "gtk",        "settings.ini",    "/.config/gtk-3.0/settings.ini",
        "gtk",        "settings.ini",    "/.config/gtk-4.0/settings.ini",
        "gtk",        "gtk.css",         "/.config/gtk-3.0/gtk.css",
        "gtk",        "gtk4.css",        "/.config/gtk-4.0/gtk.css",
        "gtk",        "gtkrc-2.0",       "/.gtkrc-2.0",
        "xsettingsd", "xsettingsd.conf", "/.config/xsettingsd/xsettingsd.conf",
        "qtct",       "qt6ct.conf",      "/.config/qt6ct/qt6ct.conf",
        "qtct",       "colors.conf",     "/.config/qt6ct/colors/rice.conf",
        "qtct",       "qt5ct.conf",      "/.config/qt5ct/qt5ct.conf",
        "qtct",       "colors.conf",     "/.config/qt5ct/colors/rice.conf",
        NULL
    };
    /* The gsettings key and the theme.list field that fills it. GTK4/libadwaita
     * reads gsettings, not settings.ini; the names come straight from theme.list
     * rather than being parsed back out of the file this module just wrote --
     * one source, no round trip. */
    static const char *const gkeys[] = {
        "gtk-theme",    "gtk_theme",
        "icon-theme",   "icon_theme",
        "cursor-theme", "cursor_theme",
        "font-name",    "ui_font",
        NULL
    };
    Str src, dst;
    size_t i;
    int ok;

    /* Packages only on a real install. Every verb underneath is neutralised on
     * a theme pass anyway, so these three steps used to run as three lines of
     * "Installing ..." that installed nothing -- and any one of them that
     * reported failure said "Installing ... failed" on a pass that was never
     * allowed to install. */
    if (!osr_theme_only()) {
        ok = osr_pkg_install_step("Installing theming stack", stack);
        ok = osr_pkg_install_step("Installing fonts", fonts) && ok;
        ok = osr_step("Installing JetBrains Mono Nerd Font", nerd_font,
                      (void *)"JetBrainsMono") && ok;
    }

    /* The env var that selects qt5ct/qt6ct. The xprofile layer sets it too, but
     * only an X11 session reads that file: GNOME/KDE on Wayland launch apps from
     * the systemd user session, whose only env source is
     * ~/.config/environment.d. Not theme-owned -- one constant value, so it is a
     * plain layer, not a template. */
    str_initv(&src, &dst, (Str *)NULL);
    str_addzz(&src, osr_mod_dotfiles(), "/environment.d/90-qt.conf", (const char *)NULL);
    str_addzz(&dst, osr_mod_home(), "/.config/environment.d/90-qt.conf", (const char *)NULL);
    ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    if (*osr_mod_theme() == '\0') { str_free(&src); str_free(&dst); return ok; }

    /* --- GTK 2/3/4 and Qt5/6 ---------------------------------------------- */
    for (i = 0; layers[i] != NULL; i += 3) {
        str_setz(&dst, osr_mod_home(), layers[i + 2], (const char *)NULL);
        (void)osr_install_theme_layer(layers[i], layers[i + 1], str_text(&dst));
    }

    /* The named themes behind those files, before the keys that name them. */
    {
        static const char *const named[] = {
            "themes", "gtk_theme",    "GTK theme",
            "icons",  "icon_theme",   "icon theme",
            "icons",  "cursor_theme", "cursor theme",
            NULL
        };
        Str name;
        str_init(&name);
        for (i = 0; named[i] != NULL; i += 3) {
            str_reset(&name);
            osr_theme_meta(&name, osr_mod_theme(), named[i + 1]);
            want_theme(named[i], str_text(&name), named[i + 2]);
        }
        str_free(&name);
    }

    if (!osr_have_cmd(osr_gsettings())) {
        /* Not an error on a WM that has no settings daemon -- that is what the
         * files above are for -- but on GNOME it means the desktop keeps its
         * old colours and nothing else in this run would have said so. */
        osr_warnf("gsettings not on PATH - GNOME keys (accent, gtk/icon theme) "
                  "left alone");
    } else {
        Str value;
        str_init(&value);
        for (i = 0; gkeys[i] != NULL; i += 2) {
            str_reset(&value);
            osr_theme_meta(&value, osr_mod_theme(), gkeys[i + 1]);
            if (value.len == 0) continue;
            gset(gkeys[i], str_text(&value));
        }
        str_reset(&value);
        str_addz(&value, "prefer-");
        osr_theme_meta(&value, osr_mod_theme(), "polarity");
        gset("color-scheme", str_text(&value));
        /* GNOME 47+ tints its own shell chrome from a NAMED accent (there is no
         * hex key), so the theme names the nearest one. Older GNOME ignores it. */
        str_reset(&value);
        osr_theme_meta(&value, osr_mod_theme(), "gnome_accent");
        if (value.len > 0) gset("accent-color", str_text(&value));
        str_free(&value);
    }

    /* --- cursor theme: the root window needs telling separately ------------ */
    str_setz(&dst, osr_mod_home(), "/.local/share/icons/default/index.theme",
        (const char *)NULL);
    (void)osr_install_theme_layer("icons", "default-index.theme", str_text(&dst));

    /* --- ~/.Xresources: dotfiles base + rice palette (§5 by composition) ----
     * Xresources has no usable include for a per-user path, so the installed
     * file is generated: the base (Xft rendering, DPI) followed by the rice's
     * color block. */
    {
        Str colors, base, body, tmp;
        int is_temp = 0;

        str_initv(&colors, &base, &body, &tmp, (Str *)NULL);
        str_addzz(&base, osr_mod_dotfiles(), "/xresources/Xresources", (const char *)NULL);
        if (file_exists(str_text(&base))
            && osr_theme_source(&colors, "xresources", "colors", &is_temp)) {
            char *a, *b;
            size_t alen, blen;

            a = slurp(str_text(&base), &alen);
            b = slurp(str_text(&colors), &blen);
            if (a != NULL) str_add(&body, a, alen);
            if (b != NULL) str_add(&body, b, blen);
            free(a); free(b);

            str_addzz(&tmp, env_str("TMPDIR", "/tmp"), "/osr-xresources-", (const char *)NULL);
            str_addl(&tmp, (long)getpid());
            {
                FILE *f = fopen(str_text(&tmp), "wb");
                if (f != NULL) {
                    if (body.len > 0) (void)fwrite(str_text(&body), 1, body.len, f);
                    fclose(f);
                    str_setz(&dst, osr_mod_home(), "/.Xresources", (const char *)NULL);
                    ok = osr_install_file(str_text(&tmp), str_text(&dst)) && ok;
                }
                (void)unlink(str_text(&tmp));
            }
            if (is_temp) (void)unlink(str_text(&colors));
            if (env_is_set("DISPLAY") && osr_have_cmd("xrdb")) {
                char *argv[4];
                str_setz(&dst, osr_mod_home(), "/.Xresources", (const char *)NULL);
                argv[0] = (char *)"xrdb"; argv[1] = (char *)"-merge";
                argv[2] = dst.p; argv[3] = NULL;
                (void)osr_run_user_quiet(argv);
            }
        }
        str_freev(&colors, &base, &body, &tmp, (Str *)NULL);
    }

    str_freev(&src, &dst, (Str *)NULL);
    return ok;
}
