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
#include "../lib/common.h"
#include "../lib/cmds.h"
#include "../lib/config.h"
#include "../lib/nerdfont.h"
#include "../lib/render.h"

#include <stddef.h>
#include <unistd.h>

static int nerd_font(void *ctx) { return osr_install_nerd_font((const char *)ctx); }

/* gset -- `as_user gsettings set org.gnome.desktop.interface <key> <value>`,
 * best-effort: no dconf daemon in a container, and a failure here is cosmetic
 * (§9). */
static void gset(const char *key, const char *value) {
    char *argv[6];
    argv[0] = (char *)"gsettings"; argv[1] = (char *)"set";
    argv[2] = (char *)"org.gnome.desktop.interface";
    argv[3] = (char *)key; argv[4] = (char *)value; argv[5] = NULL;
    (void)osr_run_user_quiet(argv);
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

    ok = osr_pkg_install_step("Installing theming stack", stack);
    ok = osr_pkg_install_step("Installing fonts", fonts) && ok;
    ok = osr_step("Installing JetBrains Mono Nerd Font", nerd_font,
                  (void *)"JetBrainsMono") && ok;

    /* The env var that selects qt5ct/qt6ct. The xprofile layer sets it too, but
     * only an X11 session reads that file: GNOME/KDE on Wayland launch apps from
     * the systemd user session, whose only env source is
     * ~/.config/environment.d. Not theme-owned -- one constant value, so it is a
     * plain layer, not a template. */
    str_init(&src); str_init(&dst);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/environment.d/90-qt.conf");
    str_addz(&dst, osr_mod_home());     str_addz(&dst, "/.config/environment.d/90-qt.conf");
    ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    if (*osr_mod_theme() == '\0') { str_free(&src); str_free(&dst); return ok; }

    /* --- GTK 2/3/4 and Qt5/6 ---------------------------------------------- */
    for (i = 0; layers[i] != NULL; i += 3) {
        str_reset(&dst);
        str_addz(&dst, osr_mod_home()); str_addz(&dst, layers[i + 2]);
        (void)osr_install_theme_layer(layers[i], layers[i + 1], str_text(&dst));
    }

    if (osr_have_cmd("gsettings")) {
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
    str_reset(&dst);
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.local/share/icons/default/index.theme");
    (void)osr_install_theme_layer("icons", "default-index.theme", str_text(&dst));

    /* --- ~/.Xresources: dotfiles base + rice palette (§5 by composition) ----
     * Xresources has no usable include for a per-user path, so the installed
     * file is generated: the base (Xft rendering, DPI) followed by the rice's
     * color block. */
    {
        Str colors, base, body, tmp;
        int is_temp = 0;

        str_init(&colors); str_init(&base); str_init(&body); str_init(&tmp);
        str_addz(&base, osr_mod_dotfiles()); str_addz(&base, "/xresources/Xresources");
        if (file_exists(str_text(&base))
            && osr_theme_source(&colors, "xresources", "colors", &is_temp)) {
            char *a, *b;
            size_t alen, blen;

            a = slurp(str_text(&base), &alen);
            b = slurp(str_text(&colors), &blen);
            if (a != NULL) str_add(&body, a, alen);
            if (b != NULL) str_add(&body, b, blen);
            free(a); free(b);

            str_addz(&tmp, env_str("TMPDIR", "/tmp"));
            str_addz(&tmp, "/osr-xresources-");
            str_addl(&tmp, (long)getpid());
            {
                FILE *f = fopen(str_text(&tmp), "wb");
                if (f != NULL) {
                    if (body.len > 0) (void)fwrite(str_text(&body), 1, body.len, f);
                    fclose(f);
                    str_reset(&dst);
                    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.Xresources");
                    ok = osr_install_file(str_text(&tmp), str_text(&dst)) && ok;
                }
                (void)unlink(str_text(&tmp));
            }
            if (is_temp) (void)unlink(str_text(&colors));
            if (env_is_set("DISPLAY") && osr_have_cmd("xrdb")) {
                char *argv[4];
                str_reset(&dst);
                str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.Xresources");
                argv[0] = (char *)"xrdb"; argv[1] = (char *)"-merge";
                argv[2] = dst.p; argv[3] = NULL;
                (void)osr_run_user_quiet(argv);
            }
        }
        str_free(&colors); str_free(&base); str_free(&body); str_free(&tmp);
    }

    str_free(&src); str_free(&dst);
    return ok;
}
