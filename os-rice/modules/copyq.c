/* modules/copyq.c -- clipboard manager, the X11 replacement for cliphist
 * (i3-sugg §2.1). Not optional on X11: a selection is owned by the process that
 * made it, so closing the source app destroys what you copied. CopyQ owns the
 * selection on everyone's behalf.
 *
 * Void spells it CopyQ (xbps.map carries the row); xsel covers the PRIMARY
 * selection for scripts that expect it.
 * CopyQ paints its own item list from a theme .ini, not from the Qt palette
 * (theme-owned, §6b). Installed as a loadable preset under themes/ - CopyQ keeps
 * the ACTIVE appearance inside copyq.conf, which is user territory here, so this
 * is applied once from Preferences > Appearance > Load and then swaps with the
 * theme on every later switch.
 * ...and then actually APPLY it. Shipping the preset alone means every fresh
 * install ends with the stock Qt blue-on-white list until someone finds
 * Preferences > Appearance > Load — a themed desktop with one unthemed window in
 * it, which is exactly the kind of gap §6 exists to close.
 *
 * CopyQ keeps the ACTIVE appearance in copyq.conf's [Theme] section (loading a
 * preset just copies the keys in), so the apply is: drop the old [Theme] block,
 * append ours. Everything outside that section is user territory - tabs,
 * commands, the tray behaviour - and is carried through untouched.
 *
 * Was modules/copyq.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <stdlib.h>

int osrm_copyq(void) {
    static const char *const pkgs[] = { "copyq", "xclip", "xsel", NULL };
    /* One shell for the whole rewrite, and the SAME text the sh module used: it
     * is a read-modify-write of a file CopyQ also writes, and splitting it into
     * several commands would widen the window in which both are mid-edit. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverlength-strings"
    static const char rewrite[] =
        "\n"
        "        _conf=$1\n"
        "        _theme=$2\n"
        "        _tmp=$_conf.osr-new\n"
        "        # Everything except the existing [Theme] section. A section ends at the\n"
        "        # next [Header] or at EOF.\n"
        "        awk \"BEGIN { keep = 1 } /^\\[/ { keep = (\\$0 != \\\"[Theme]\\\") } keep\" \"$_conf\" >\"$_tmp\"\n"
        "        # Our block, comments stripped - CopyQ rewrites this file itself and\n"
        "        # would not preserve them anyway.\n"
        "        printf \"\\n\" >>\"$_tmp\"\n"
        "        grep -v \"^[[:space:]]*#\" \"$_theme\" | grep -v \"^[[:space:]]*$\" >>\"$_tmp\"\n"
        "        mv -f \"$_tmp\" \"$_conf\"\n"
        "    ";
#pragma GCC diagnostic pop
    Str dir, theme, conf;
    char *argv[7];
    int ok;

    ok = osr_pkg_install_step("Installing CopyQ", pkgs);

    str_init(&dir); str_init(&theme); str_init(&conf);
    str_addz(&dir, osr_mod_home());   str_addz(&dir, "/.config/copyq");
    str_addz(&theme, str_text(&dir)); str_addz(&theme, "/themes/osr.ini");
    str_addz(&conf, str_text(&dir));  str_addz(&conf, "/copyq.conf");
    (void)osr_install_theme_layer("copyq", "theme.ini", str_text(&theme));

    if (file_exists(str_text(&theme))) {
        ok = osr_mkdir_p(str_text(&dir)) && ok;
        if (!file_exists(str_text(&conf))) {
            argv[0] = (char *)"touch"; argv[1] = conf.p; argv[2] = NULL;
            (void)osr_run_user(argv);
        }
        /* CopyQ owns copyq.conf and rewrites it wholesale, so the theme cannot
         * be a file of ours beside it: the [Theme] section is replaced in place
         * and everything else in the file is left exactly as CopyQ wrote it. */
        argv[0] = (char *)"sh"; argv[1] = (char *)"-c"; argv[2] = (char *)rewrite;
        argv[3] = (char *)"sh"; argv[4] = conf.p; argv[5] = theme.p; argv[6] = NULL;
        ok = osr_run_step_user("Applying the CopyQ theme", argv) && ok;

        /* CopyQ reads copyq.conf at start and rewrites it at exit, so a running
         * instance would both ignore the new theme and clobber it on logout. */
        {
            char *p[4];
            p[0] = (char *)"pgrep"; p[1] = (char *)"-x"; p[2] = (char *)"copyq"; p[3] = NULL;
            if (osr_run_user_quiet(p) == 0) {
                argv[0] = (char *)"sh"; argv[1] = (char *)"-c";
                argv[2] = (char *)"copyq exit >/dev/null 2>&1 || true"; argv[3] = NULL;
                (void)osr_run_user(argv);
                argv[2] = (char *)"copyq >/dev/null 2>&1 &";
                (void)osr_run_user(argv);
            }
        }
    }
    str_free(&dir); str_free(&theme); str_free(&conf);
    return ok;
}
