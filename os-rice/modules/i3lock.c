/* modules/i3lock.c -- screen lock + idle, the X11 replacement for hyprlock and
 * hypridle (i3-sugg §2).
 *
 * Three cooperating pieces, and the middle one is the piece people forget:
 *
 * betterlockscreen  the lock UI — caches a blurred/dimmed copy of the wallpaper
 * once, so unlocking is instant instead of a 1s blur
 * xss-lock          binds the lock to the X screensaver AND to the logind/
 * elogind suspend inhibitor: `--transfer-sleep-lock` is what
 * makes the screen actually be locked when the lid opens
 * the idle timer     xidlehook when a Rust toolchain is present, xautolock
 * otherwise — see the block below
 *
 * betterlockscreen is packaged on Void and on no Debian/Ubuntu release; apt.map
 * routes it to provide_betterlockscreen (lib/build.sh), which installs the
 * upstream script. It is not optional either way: it is both the xss-lock target
 * and the $mod+Escape binding in the shipped i3 config.
 *
 * The rice's betterlockscreenrc carries the colors; the wallpaper cache is
 * primed here, best-effort, because it needs a running X server.
 * The idle timer, and the one real difference between the two options: xautolock
 * counts wall-clock idle time and nothing else, so it blanks the screen ten
 * minutes into a film. xidlehook's --not-when-audio / --not-when-fullscreen are
 * the inhibits every full desktop honours and the fix for i3-sugg §12 gotcha 14.
 *
 * It is Rust-only (cargo: on both xbps and apt), so it needs `rust` earlier in
 * the manifest — which the i3 rices list. Where that toolchain is missing the
 * module falls back rather than failing, and the i3 config probes for the binary
 * at session start, so both paths produce a working idle lock.
 * cargo lives in the TARGET USER's home (modules/rust.sh installs rustup there),
 * not on root's PATH, so probe the path _via_cargo itself uses rather than
 * `command -v cargo` - which is false under `as_root` even on a machine that has
 * Rust. The same applies to the result: cargo installs into ~/.cargo/bin.
 * custom-pre.sh is the LAYOUT half, and it is separate from the rc because
 * betterlockscreen keeps them separate: the rc carries colours, and positions
 * are not among the variables it reads at all. betterlockscreen sources this
 * from prelock() and appends its `lockargs` last, which is the only supported
 * way to override its built-in bottom-left placement.
 * Prime the blur cache from this rice's wallpaper. Needs X, so it degrades to a
 * note when headless (§9) — betterlockscreen re-caches on first use anyway.
 *
 * Was modules/i3lock.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int osrm_i3lock(void) {
    static const char *const pkgs[] = {
        "betterlockscreen", "i3lock-color", "xss-lock", NULL
    };
    static const char *const deps[] = { "xidlehook-build-deps", NULL };
    static const char *const xidle[] = { "xidlehook", NULL };
    static const char *const xauto[] = { "xautolock", NULL };
    Str cargo, bin, src, dst, wp;
    char *argv[7];
    int ok;

    ok = osr_pkg_install_step("Installing lock screen", pkgs);

    str_init(&cargo); str_init(&bin);
    str_addz(&cargo, osr_mod_home()); str_addz(&cargo, "/.cargo/bin/cargo");
    str_addz(&bin, osr_mod_home());   str_addz(&bin, "/.cargo/bin/xidlehook");
    argv[0] = (char *)"test"; argv[1] = (char *)"-x"; argv[2] = cargo.p; argv[3] = NULL;
    if (osr_run_user(argv) == 0) {
        Str rhs;
        /* xidlehook links xcb + libpulse, so the headers have to be there
         * before cargo runs. Only xbps.map and apt.map carry the row (the i3
         * rices require one of those); an unmapped name passes through
         * unchanged and would try to install a package literally called
         * `xidlehook-build-deps`, so check first rather than let pkg_install
         * hard-fail the module on a third distro. */
        str_init(&rhs);
        osr_pkgmap_resolve(&rhs, "xidlehook-build-deps");
        if (strcmp(str_text(&rhs), "xidlehook-build-deps") != 0)
            ok = osr_pkg_install_step("Installing xidlehook build deps", deps) && ok;
        str_free(&rhs);
        ok = osr_pkg_install_step("Installing xidlehook (idle timer)", xidle) && ok;
    }
    argv[0] = (char *)"test"; argv[1] = (char *)"-x"; argv[2] = bin.p; argv[3] = NULL;
    if (osr_run_user(argv) == 0) {
        osr_info("xidlehook installed - the idle timer honours audio/fullscreen inhibits");
    } else {
        /* xautolock has no inhibit support at all, so a video goes dark
         * mid-play - which is why it is the fallback and not the default. */
        osr_info("no xidlehook (no Rust toolchain) - installing xautolock instead");
        ok = osr_pkg_install_step("Installing xautolock", xauto) && ok;
    }

    str_init(&src); str_init(&dst);
    if (*osr_mod_theme_dir() != '\0') {
        static const char *const files[] = { "betterlockscreenrc", "custom-pre.sh", NULL };
        size_t i;
        for (i = 0; files[i] != NULL; i++) {
            str_reset(&src); str_reset(&dst);
            str_addz(&src, osr_mod_theme_dir());
            str_addz(&src, "/config/betterlockscreen/"); str_addz(&src, files[i]);
            str_addz(&dst, osr_mod_home());
            str_addz(&dst, "/.config/betterlockscreen/"); str_addz(&dst, files[i]);
            if (file_exists(str_text(&src)))
                ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
        }
    }

    /* betterlockscreen pre-renders the dim/blur into a cache; without it the
     * first lock takes seconds with a blank screen. Needs an X connection, so
     * it is skipped (and said so) on a headless run. */
    str_init(&wp);
    osr_install_wallpaper(&wp);
    if (wp.len > 0 && *env_str("DISPLAY", "") != '\0' && osr_have_cmd("betterlockscreen")) {
        argv[0] = (char *)"betterlockscreen"; argv[1] = (char *)"-u"; argv[2] = wp.p;
        argv[3] = (char *)"--fx"; argv[4] = (char *)"dimblur"; argv[5] = NULL;
        if (!osr_run_step_user("Caching lock screen wallpaper", argv))
            osr_warn("betterlockscreen cache failed - it will rebuild on first lock");
    } else if (wp.len > 0) {
        osr_infof("no DISPLAY - skipping lock screen cache (run: betterlockscreen -u '%s')",
                  str_text(&wp));
    }
    str_free(&cargo); str_free(&bin); str_free(&src); str_free(&dst); str_free(&wp);
    return ok;
}
