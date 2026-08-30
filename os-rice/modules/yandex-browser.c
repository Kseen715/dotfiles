/* modules/yandex-browser.c -- Yandex Browser + a low-RAM flags layer. ONE copy,
 * POSIX.
 *
 * Install route by target (§1, all of it in pkgmap):
 *   pacman  aur:yandex-browser
 *   apt     source:provide_yandex_browser -- the vendor's own apt repo, which no
 *           Debian/Ubuntu archive carries
 *   xbps    source:provide_yandex_browser_deb -- Void packages it nowhere and
 *           the vendor ships deb/rpm only, so the .deb is unpacked into /opt
 * Anything else has no package and fails loudly rather than installing a
 * lookalike, same convention as vscode on apt.
 *
 * Config split (§5). Yandex Browser is Chromium, so there is no user.js to
 * write: the memory knobs are command-line switches, and the only place a switch
 * can be attached without touching /usr is the .desktop entry. dotfiles owns the
 * switch list (yandex-browser/flags.conf) and this module stamps it into a
 * user-level copy of each launcher in ~/.local/share/applications, which XDG
 * resolves before the packaged one. No rice layer: Chromium takes no user
 * stylesheet.
 *
 * Consequence worth knowing: the flags reach the browser when it is started from
 * the menu/rofi/a mailto handler, i.e. every normal launch. Typing
 * `yandex-browser` in a terminal bypasses the .desktop entry and gets defaults.
 *
 * Was modules/yandex-browser.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <glob.h>
#include <stddef.h>

/* flatten_flags -- the sh module's sed/tr pipeline: one switch per line with
 * `#` comments, the comments dropped, the lines joined, runs of spaces squeezed
 * and the ends trimmed -- a single argument string. */
static void flatten_flags(Str *out, const char *text, size_t len) {
    Str joined;
    const char *p;
    size_t i;
    int space = 0;

    str_init(&joined);
    for (i = 0; i < len; i++) {
        if (text[i] == '#') {                       /* to the end of that line */
            while (i < len && text[i] != '\n') i++;
            if (i >= len) break;
        }
        str_addc(&joined, text[i] == '\n' ? ' ' : text[i]);
    }
    /* tr -s ' ': runs of spaces collapse; then the ends are trimmed. */
    p = str_text(&joined);
    for (i = 0; p[i] != '\0'; i++) {
        if (p[i] == ' ') { space = 1; continue; }
        if (space && out->len > 0) str_addc(out, ' ');
        space = 0;
        str_addc(out, p[i]);
    }
    str_free(&joined);
}

/* stamp_exec -- `sed "s|^Exec=\\([^ ]*\\)|Exec=\\1 <flags>|"`: insert the
 * switches straight after the binary in every Exec= line ([Desktop Action]
 * entries included) and before the %U field code -- positional arguments after
 * it are URLs, not switches. */
static void stamp_exec(Str *out, const char *text, size_t len, const char *flags) {
    size_t pos = 0;
    Line line;

    while (next_line(text, len, &pos, &line)) {
        if (line.len >= 5 && strncmp(line.start, "Exec=", 5) == 0) {
            size_t i = 5;
            while (i < line.len && line.start[i] != ' ') i++;
            str_add(out, line.start, i);
            str_addc(out, ' ');
            str_addz(out, flags);
            str_add(out, line.start + i, line.len - i);
        } else {
            str_add(out, line.start, line.len);
        }
        str_addc(out, '\n');
    }
}

int osrm_yandex_browser(void) {
    static const char *const pkgs[] = { "yandex-browser", NULL };
    Str path, flags, apps, dst, body;
    char *buf;
    size_t len;
    int ok;
    int done = 0;

    ok = osr_pkg_install_step("Installing Yandex Browser", pkgs);

    str_init(&path);
    str_addz(&path, osr_mod_dotfiles()); str_addz(&path, "/yandex-browser/flags.conf");
    buf = slurp(str_text(&path), &len);
    if (buf == NULL) { str_free(&path); return ok; }

    str_init(&flags);
    flatten_flags(&flags, buf, len);
    free(buf);

    str_init(&apps);
    str_addz(&apps, osr_mod_home()); str_addz(&apps, "/.local/share/applications");
    ok = osr_mkdir_p(str_text(&apps)) && ok;

    str_init(&dst); str_init(&body);
    {
        /* A variable only so the unit test can aim at a fixture dir. */
        const char *dirs = env_str("OSR_DESKTOP_DIRS",
                                   "/usr/share/applications /usr/local/share/applications");
        const char *p = dirs;
        while (*p != '\0') {
            const char *start;
            Str pattern;
            glob_t g;
            size_t i;

            while (*p == ' ') p++;
            start = p;
            while (*p != '\0' && *p != ' ') p++;
            if (p == start) continue;

            str_init(&pattern);
            str_add(&pattern, start, (size_t)(p - start));
            /* Two entries ship with the deb under different names -- the
             * reverse-DNS ru.yandex.desktop.browser.desktop and the plain
             * yandex-browser.desktop (the one that is the http handler). Both
             * exec the same binary and both get the flags: which one a launcher
             * picks is not ours to predict. */
            str_addz(&pattern, "/*yandex*browser*.desktop");
            if (glob(str_text(&pattern), 0, NULL, &g) == 0) {
                for (i = 0; i < g.gl_pathc; i++) {
                    Str base;
                    char *entry;
                    size_t elen;

                    if (!file_exists(g.gl_pathv[i])) continue;
                    entry = slurp(g.gl_pathv[i], &elen);
                    if (entry == NULL) continue;

                    str_init(&base);
                    base_of(&base, g.gl_pathv[i]);
                    osr_infof("installing low-RAM launcher: %s", str_text(&base));

                    str_reset(&body);
                    stamp_exec(&body, entry, elen, str_text(&flags));
                    free(entry);

                    str_reset(&dst);
                    str_addz(&dst, str_text(&apps)); str_addc(&dst, '/');
                    str_addz(&dst, str_text(&base));
                    ok = osr_write_user(str_text(&dst), str_text(&body)) && ok;
                    done = 1;
                    str_free(&base);
                }
            }
            globfree(&g);
            str_free(&pattern);
        }
    }

    /* The browser is installed either way, but without a launcher nothing
     * carries the flags -- say so instead of leaving the tuning silently
     * unapplied. */
    if (!done)
        osr_warn("no yandex-browser .desktop found - the low-RAM flags are not "
                 "applied (rerun this module after the install)");
    if (osr_have_cmd("update-desktop-database")) {
        char *argv[3];
        argv[0] = (char *)"update-desktop-database"; argv[1] = apps.p; argv[2] = NULL;
        (void)osr_run_user_quiet(argv);
    }

    str_free(&path); str_free(&flags); str_free(&apps); str_free(&dst); str_free(&body);
    return ok;
}
