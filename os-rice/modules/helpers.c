/* modules/helpers.c -- the "preferred application" layer: which program the
 * REST of the desktop launches when it needs a terminal or a file manager, and
 * a guarantee that the answer resolves to something installed.
 *
 * Every desktop environment has this and i3 has none of it, which produces the
 * most confusing class of bug in an i3 desktop: a menu entry that is present,
 * enabled, and does nothing at all.
 *
 *   Thunar's "Open Terminal Here" is not a Thunar setting. Thunar shells out to
 *   `exo-open --launch TerminalEmulator`, and exo resolves that role through
 *   ~/.config/xfce4/helpers.rc. Outside XFCE nothing ever writes that file, so
 *   exo has no candidate, exits non-zero, and Thunar reports nothing.
 *
 * Three things, in the order they depend on each other:
 *
 *   1. exo            the resolver itself (Debian splits the tools out as
 *                     exo-utils; apt.map carries the row)
 *   2. a helper entry osr-term (i3/scripts/term.sh) is the session's one
 *      for osr-term   terminal launcher, and nothing ships an X-XFCE-Helper
 *                     .desktop for it, so the role would resolve to a name exo
 *                     cannot expand. Pointing the role at osr-term rather than
 *                     at alacritty directly is deliberate: exo gets the same
 *                     degrade-to-something-that-runs behaviour as $mod+Return,
 *                     instead of its own second opinion about which terminal
 *                     this machine has.
 *   3. xterm          the escape hatch. Every terminal a rice would pick is
 *                     GPU-accelerated, so any of them can fail on a machine
 *                     where the rest of the session is fine. One package is the
 *                     difference between "my terminal is broken" and "I cannot
 *                     open a terminal to find out why".
 *
 * Written in C rather than as a .sh module because it is a new module and C is
 * the tier this harness is moving to (lib/modules.c); every package it installs
 * resolves natively on both of this rice's targets, which is what osr_pkg_install
 * requires -- no cargo:/source: row can reach it.
 *
 * Both files are SEEDED, not installed as layers (§5): which terminal opens is
 * a preference, and the user editing helpers.rc must be the last word.
 *
 * Linux-only; there is no #ifdef _WIN32 half. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

/* The role -> helper-id map exo reads. The values are helper IDS, not binaries:
 * exo expands each through a .desktop under /usr/share/xfce4/helpers/. */
static const char helpers_rc[] =
    "# Seeded once by os-rice (modules/helpers.c) - yours to edit, never rewritten.\n"
    "# Role -> helper id. exo-open --launch <role> resolves through this file;\n"
    "# the ids expand via /usr/share/xfce4/helpers/<id>.desktop.\n"
    "TerminalEmulator=osr-term\n"
    "FileManager=Thunar\n";

/* The helper entry upstream does not ship. X-XFCE-CommandsWithParameter is the
 * form used when a caller passes a command to run in the new terminal ("Open
 * Terminal Here" passes none, and falls back to X-XFCE-Commands). */
static const char osrterm_helper[] =
    "[Desktop Entry]\n"
    "Version=1.0\n"
    "Encoding=UTF-8\n"
    "Type=X-XFCE-Helper\n"
    "X-XFCE-Category=TerminalEmulator\n"
    "X-XFCE-CommandsWithParameter=osr-term -e \"%s\";\n"
    "X-XFCE-Commands=osr-term;\n"
    "Icon=utilities-terminal\n"
    "Name=Terminal\n";

int osrm_helpers(void) {
    static const char *const pkgs[] = { "exo", "xterm", NULL };
    Str path;
    int ok;

    ok = osr_pkg_install_step("Installing desktop helper resolution", pkgs);

    str_init(&path);
    str_addz(&path, osr_mod_home());
    str_addz(&path, "/.config/xfce4/helpers.rc");
    if (!osr_seed_file(str_text(&path), helpers_rc)) {
        osr_warnf("could not seed %s - Thunar's \"Open Terminal Here\" will do nothing",
                  str_text(&path));
        ok = 0;
    }
    str_free(&path);

    /* Root-owned on purpose: a helper id is resolved from the system dir, and
     * the per-user ~/.local/share/xfce4/helpers path is not read by every exo
     * build. The seed only writes when nothing is there, so a distro that ever
     * starts shipping this id wins. */
    if (!osr_seed_file_root("/usr/share/xfce4/helpers/osr-term.desktop", osrterm_helper)) {
        osr_warnf("could not write the osr-term helper entry - "
                  "set TerminalEmulator in ~/.config/xfce4/helpers.rc to a packaged terminal");
        ok = 0;
    }
    return ok;
}
