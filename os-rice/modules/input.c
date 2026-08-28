/* modules/input.c -- pointer, keyboard and remapping (i3-sugg §5). The X server
 * and xkb data come from modules/xorg.sh; this is the layer on top of them:
 * gestures, dual-role keys, numlock, per-window layout.
 *
 * libinput-gestures  three/four-finger swipes -> i3 workspace switching
 * xcape              tap Ctrl for Escape, hold it for Ctrl (X11-level)
 * keyd               the same idea at the kernel level, so it also works in a
 * TTY, in games that grab the keyboard, and under Wayland
 * kbdd               remembers the keyboard layout per window
 * numlockx           numlock on at session start (X has no BIOS state)
 *
 * Deliberately NOT installed: `interception-tools` and `kmonad` are the other two
 * kernel-level remappers, and running two of them at once fights over the same
 * evdev grabs. Pick one — `osr module input` gives you keyd; swap it here if you
 * prefer another. `fusuma` is Ruby-gem-only and is not packaged on Void.
 * libinput-gestures needs the user in the `input` group to read /dev/input.
 * keyd is a system daemon with a root-owned config; seeded once, then yours.
 *
 * Port of modules/input.sh, kept as the reference at
 * test/ref/input_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int osrm_input(void) {
    static const char *const pkgs[] = {
        "xf86-input-libinput", "xkeyboard-config", "setxkbmap", "xorg-xmodmap",
        "libinput-gestures", "xcape", "keyd", "kbdd", "numlockx", NULL
    };
    Str src, dst, groups;
    char *argv[5];
    int ok;

    ok = osr_pkg_install_step("Installing input tools", pkgs);

    /* libinput-gestures reads /dev/input directly, which is group-owned. */
    if (osr_have_cmd("libinput-gestures")) {
        int member = 0;
        str_init(&groups);
        argv[0] = (char *)"id"; argv[1] = (char *)"-nG";
        argv[2] = (char *)osr_mod_user(); argv[3] = NULL;
        if (osr_run_capture(argv, &groups)) {
            /* Whole-word: a group called "inputrc" is not the input group. */
            const char *p = str_text(&groups);
            while (!member && (p = strstr(p, "input")) != NULL) {
                int left  = p == str_text(&groups) || is_space(p[-1]);
                int right = p[5] == '\0' || is_space(p[5]);
                member = left && right;
                p += 5;
            }
        }
        str_free(&groups);
        if (member) {
            osr_infof("%s already in the input group - skipping", osr_mod_user());
        } else {
            osr_infof("adding %s to the input group (libinput-gestures needs /dev/input)",
                      osr_mod_user());
            argv[0] = (char *)"usermod"; argv[1] = (char *)"-aG";
            argv[2] = (char *)"input"; argv[3] = (char *)osr_mod_user(); argv[4] = NULL;
            if (osr_run_root(argv) != 0)
                osr_warnf("could not add %s to input", osr_mod_user());
        }
    }

    str_init(&src); str_init(&dst);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/input/libinput-gestures.conf");
    str_addz(&dst, osr_mod_home());     str_addz(&dst, "/.config/libinput-gestures.conf");
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    /* keyd's config is SEEDED, not owned: it remaps keys, and a user who has
     * edited it must not have that overwritten by a rerun. */
    str_reset(&src);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/input/keyd-default.conf");
    if (!file_exists("/etc/keyd/default.conf") && file_exists(str_text(&src))) {
        osr_info("seeding /etc/keyd/default.conf");
        argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
        argv[2] = (char *)"/etc/keyd"; argv[3] = NULL;
        (void)osr_run_root(argv);
        argv[0] = (char *)"cp"; argv[1] = (char *)"-f"; argv[2] = src.p;
        argv[3] = (char *)"/etc/keyd/default.conf"; argv[4] = NULL;
        (void)osr_run_root(argv);
    }
    str_free(&src); str_free(&dst);

    if (!osr_service_enable("keyd")) osr_warn("could not enable keyd (needs a real init)");
    return ok;
}
