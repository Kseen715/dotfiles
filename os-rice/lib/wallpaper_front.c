/* lib/wallpaper_front.c -- the C behind wallpaper.sh: the front end that sets
 * or queries the wallpaper of the current theme (§6a).
 *
 *   osr wallpaper                print the wallpaper in use
 *   osr wallpaper --list         print the library
 *   osr wallpaper <path>         make <path> the wallpaper of the current theme
 *   osr wallpaper --next         step to the next image in the library
 *
 * Named wallpaper_front.c and not wallpaper.c because that name is taken by
 * the Windows tier's own unit (lib/wallpaper.c, painting the desktop through
 * SystemParametersInfo). This one has no wallpaper logic of its own at all:
 * install, record, set-live, library and choose are lib/config.c's, the same
 * functions the sh original called through lib/config.sh. What is here is
 * exactly what wallpaper.sh was -- an option loop, the current-theme
 * resolution, and four small actions over that library.
 *
 * What this front end must do is stated in test/unit_c/wallpaper_test.c.
 * Two places where it deliberately improves on the sh original it replaced
 * are documented below: an option missing its operand, and --next over a
 * path with a space in it.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "cmds.h"
#include "config.h"
#include "module.h"

#include <unistd.h>

static const char *root_dir(void) { return env_str("OSR_ROOT", "."); }

/* usage -- wallpaper.sh printed its own header comment (`sed -n '2,9p' | sed
 * 's/^# \{0,1\}//'`), so the help text and the file's documentation could not
 * drift apart. Here they are the same lines, held together by ui_test.c's
 * D-2 check instead.
 *
 * ASCII only, and that is a CHANGE from what the sh original printed: its
 * header carried an em dash and a section sign, and the byte-for-byte port
 * carried them across as octal escapes. D-2 says every byte written to the
 * terminal is 7-bit, and this was the one place in the tree that broke it --
 * unnoticed because the lint that enforces D-2 only ever scanned .sh files. */
static void usage(void) {
    fputs("wallpaper.sh - set or query the wallpaper of the current theme (SS6a).\n", stdout);
    fputs("\n", stdout);
    fputs("  wallpaper.sh                 print the wallpaper in use\n", stdout);
    fputs("  wallpaper.sh --list          print the library (theme images + ~/Pictures/Wallpapers)\n", stdout);
    fputs("  wallpaper.sh <path>          make <path> the wallpaper of the current theme\n", stdout);
    fputs("  wallpaper.sh --next          step to the next image in the library\n", stdout);
    fputs("\n", stdout);
    fputs("Separate from install.sh because it is not an install: no modules run, no\n", stdout);
}

/* print_line -- `printf '%s\n' "${x:-<dflt>}"`. */
static void print_line(const char *text, const char *dflt) {
    fputs((text != NULL && *text != '\0') ? text : dflt, stdout);
    fputc('\n', stdout);
}

/* resolve_theme -- the current theme decides which wallpapers are on offer and
 * which key the choice is stored under. With no theme applied yet, the default;
 * with one recorded that no longer exists, a fatal error rather than a silent
 * fallback -- the library it would offer would be another theme's. */
static void resolve_theme(void) {
    Str theme, dir;

    str_init(&theme);
    osr_state_get(&theme, "theme");
    if (theme.len == 0) str_addz(&theme, env_str("OSR_DEFAULT_THEME", "xin"));
    if (!osr_theme_exists(str_text(&theme)))
        osr_die("recorded theme '%s' no longer exists (see: osr themes)", str_text(&theme));

    str_init(&dir);
    str_addz(&dir, root_dir());
    str_addz(&dir, "/themes/");
    str_addz(&dir, str_text(&theme));
    setenv("OSR_THEME", str_text(&theme), 1);
    setenv("OSR_THEME_DIR", str_text(&dir), 1);
    str_free(&dir);
    str_free(&theme);
}

/* next_in_library -- the wrap-around step a single hotkey cycles wallpapers
 * with. The current image is matched by BASENAME, because the library holds the
 * theme's copy and ~/Pictures/Wallpapers' copy of the same file under one name;
 * a current one that is not in the library (or none at all) starts at the first.
 *
 * The sh original walked `for _img in $(osr_wallpaper_library)`, so a path
 * containing a space was two entries and neither existed. This walks the
 * library a line at a time, which is what the list always meant. */
static void next_in_library(Str *out) {
    Str lib, cur, cur_base, first, base;
    size_t pos = 0;
    Line l;
    int take = 0;

    str_init(&lib);
    osr_wallpaper_library(&lib);

    str_init(&cur);
    osr_theme_wallpaper(&cur);
    str_init(&cur_base);
    base_of(&cur_base, str_text(&cur));

    str_init(&first);
    str_init(&base);
    while (next_line(str_text(&lib), lib.len, &pos, &l)) {
        Str img;
        str_init(&img);
        str_add(&img, l.start, l.len);
        if (first.len == 0) str_add(&first, str_text(&img), img.len);
        if (take) {
            str_add(out, str_text(&img), img.len);
            str_free(&img);
            break;
        }
        str_reset(&base);
        base_of(&base, str_text(&img));
        if (cur.len > 0 && strcmp(str_text(&base), str_text(&cur_base)) == 0) take = 1;
        str_free(&img);
    }
    if (out->len == 0) str_add(out, str_text(&first), first.len);

    str_free(&base);
    str_free(&first);
    str_free(&cur_base);
    str_free(&cur);
    str_free(&lib);
}

int osr_wallpaper_main(int argc, char **argv) {
    const char *user = "";
    const char *action = "show";
    const char *target = "";
    int i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--user") == 0) {
            /* The sh original expanded ${2:?--user needs a name} here, whose
             * message and exit status came from the shell itself; this is a
             * normal error line, exactly as install.sh's port made it. */
            if (i + 1 >= argc) osr_die("--user needs a name");
            user = argv[++i];
        } else if (strcmp(a, "--list") == 0) {
            action = "list";
        } else if (strcmp(a, "--next") == 0) {
            action = "next";
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage();
            return 0;
        } else if (a[0] == '-') {
            osr_die("unknown option: %s", a);
        } else {
            action = "set";
            target = a;
        }
    }

    osr_resolve_user(user);
    resolve_theme();

    if (strcmp(action, "show") == 0) {
        Str cur;
        str_init(&cur);
        osr_theme_wallpaper(&cur);
        print_line(str_text(&cur), "(none)");
        str_free(&cur);
        return 0;
    }

    if (strcmp(action, "list") == 0) {
        Str lib;
        str_init(&lib);
        osr_wallpaper_library(&lib);
        fwrite(str_text(&lib), 1, lib.len, stdout);
        str_free(&lib);
        return 0;
    }

    if (strcmp(action, "next") == 0) {
        Str next, installed;

        str_init(&next);
        next_in_library(&next);
        if (next.len == 0)
            osr_die("no wallpapers found for theme '%s'", env_str("OSR_THEME", ""));

        str_init(&installed);
        osr_choose_wallpaper(&installed, str_text(&next));
        str_free(&installed);
        print_line(str_text(&next), "");
        str_free(&next);
        return 0;
    }

    /* set */
    if (access(target, F_OK) != 0) osr_die("no such file: %s", target);
    {
        Str installed;
        str_init(&installed);
        osr_choose_wallpaper(&installed, target);
        str_free(&installed);
    }
    osr_successf("wallpaper set for theme '%s': %s", env_str("OSR_THEME", ""), target);
    return 0;
}
