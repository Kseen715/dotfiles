/* modules/ghostty.c -- Ghostty terminal + JetBrains Mono Nerd Font + layered
 * config. ONE copy, POSIX, distro-agnostic. Native-first: native on arch/void
 * and recent Ubuntu; elsewhere a community binary (Fedora COPR, ghostty-ubuntu
 * .deb) and, as the last resort, built from source with a bootstrapped Zig
 * toolchain (source:provide_ghostty via pkgmap). The source build is heavy (a
 * full Zig compile) and is a real-desktop concern (§9), not container-tested.
 *
 * Config is split by ownership (§5), same shape as foot:
 *
 *   config          dotfiles-owned (10-layer) -- overwritten on update; carries
 *                   the ssh-comfort settings (terminfo, OSC 52 clipboard) and
 *                   the 0.75 transparency
 *   ghostty-theme   rice-owned palette (90-layer) -- swapped on rice switch (§6),
 *                   falling back to the dotfiles default when a rice ships none
 *
 * `config` ends with `config-file = ?ghostty-theme`, so the palette layer swaps
 * independently of the base -- the §5 split applied to a DE config. The '?'
 * keeps a missing palette from being a startup error.
 *
 * Port of modules/ghostty.sh, kept as the reference at
 * test/ref/ghostty_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/nerdfont.h"

#include <stddef.h>

static int nerd_font(void *ctx) { return osr_install_nerd_font((const char *)ctx); }

/* version_line -- the sh module's `ghostty +version`, sed-stripped of its
 * leading `Version:` and its padding, first such line only. Appends nothing
 * when no line announces one, which is the empty `$(...)` the sh module then
 * defaulted to "0". */
static void version_line(Str *out, const char *text) {
    size_t len = strlen(text);
    size_t pos = 0;
    Line line;

    while (next_line(text, len, &pos, &line)) {
        size_t i;
        if (line.len < 8) continue;
        if (line.start[0] != 'V' && line.start[0] != 'v') continue;
        if (strncmp(line.start + 1, "ersion:", 7) != 0) continue;
        i = 8;
        while (i < line.len && line.start[i] == ' ') i++;
        str_add(out, line.start + i, line.len - i);
        return;
    }
}

/* split_ver -- the sh module's parameter expansions, quirks included:
 * ${v%%.*} and ${v#*.} both yield the WHOLE string when there is no dot, so
 * "1" reads as 1.1 there and here. A pair that is not all digits reads as
 * 0.0, which is the `case ... *[!0-9]*|""` arm. */
static void split_ver(const char *ver, int *major, int *minor) {
    const char *dot = strchr(ver, '.');
    const char *rest = (dot != NULL) ? dot + 1 : ver;
    const char *dot2 = strchr(rest, '.');
    size_t mlen = (dot != NULL) ? (size_t)(dot - ver) : strlen(ver);
    size_t nlen = (dot2 != NULL) ? (size_t)(dot2 - rest) : strlen(rest);
    size_t i;

    *major = 0; *minor = 0;
    if (mlen == 0 && nlen == 0) return;
    for (i = 0; i < mlen; i++) if (ver[i] < '0' || ver[i] > '9') return;
    for (i = 0; i < nlen; i++) if (rest[i] < '0' || rest[i] > '9') return;
    for (i = 0; i < mlen; i++) *major = *major * 10 + (ver[i] - '0');
    for (i = 0; i < nlen; i++) *minor = *minor * 10 + (rest[i] - '0');
}

int osrm_ghostty(void) {
    static const char *const pkgs[] = { "ghostty", "unzip", "fontconfig", NULL };
    Str src, dst, out, ver;
    const char *features = "";
    char *argv[6];
    int major = 0, minor = 0;
    int ok;

    ok = osr_pkg_install_step("Installing Ghostty", pkgs);
    ok = osr_step("Installing JetBrains Mono Nerd Font", nerd_font,
                  (void *)"JetBrainsMono") && ok;

    str_init(&src); str_init(&dst);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/ghostty/config");
    str_addz(&dst, osr_mod_home());     str_addz(&dst, "/.config/ghostty/config");
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    /* --- shell-integration features, gated on the installed version ---------
     * `shell-integration-features` is all-or-nothing: one unrecognised value and
     * Ghostty discards the entire key and shows a config error at the top of
     * every window. ssh-env and ssh-terminfo are 1.2+, so on an older build
     * asking for them costs `sudo` and `title` as well as the ssh comfort -- a
     * strictly worse terminal than saying nothing. Hence: probe, then write only
     * what this build knows. */
    str_init(&out); str_init(&ver);
    argv[0] = (char *)"ghostty"; argv[1] = (char *)"+version"; argv[2] = NULL;
    (void)osr_run_capture(argv, &out);
    version_line(&ver, str_text(&out));
    split_ver(ver.len > 0 ? str_text(&ver) : "0", &major, &minor);

    if (major > 1 || (major == 1 && minor >= 2)) {
        features = "ssh-env,ssh-terminfo,sudo";
    } else {
        /* `sudo` is 1.1+. Below that there is nothing safe to name, and an empty
         * file leaves every feature on its default -- which is the correct
         * answer, not a degraded one. */
        if (major == 1 && minor >= 1) features = "sudo";
        osr_warnf("ghostty %s predates ssh-env/ssh-terminfo (1.2+): remote TERM fixes are off",
                  ver.len > 0 ? str_text(&ver) : "0");
    }

    str_reset(&dst);
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.config/ghostty");
    ok = osr_mkdir_p(str_text(&dst)) && ok;
    str_addz(&dst, "/ghostty-features");
    if (*features != '\0') {
        /* Rewritten, not appended: a Ghostty upgrade has to be able to turn the
         * ssh features ON, and ensure_line could only ever add a second,
         * later-winning copy of the key. */
        argv[0] = (char *)"sh"; argv[1] = (char *)"-c";
        argv[2] = (char *)"printf \"shell-integration-features = %s\\n\" \"$1\" >\"$2\"";
        argv[3] = (char *)"sh"; argv[4] = (char *)features; argv[5] = NULL;
        {
            char *full[7];
            size_t i;
            for (i = 0; i < 5; i++) full[i] = argv[i];
            full[5] = dst.p; full[6] = NULL;
            (void)osr_run_user(full);
        }
    } else {
        argv[0] = (char *)"sh"; argv[1] = (char *)"-c"; argv[2] = (char *)":>\"$1\"";
        argv[3] = (char *)"sh"; argv[4] = dst.p; argv[5] = NULL;
        (void)osr_run_user(argv);
    }

    /* Palette (rice-owned theme, swapped on switch §6). Rice override wins; the
     * dotfiles default covers a rice that ships no palette. */
    str_reset(&dst);
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.config/ghostty/ghostty-theme");
    if (!osr_install_theme_layer("ghostty", "ghostty-theme", str_text(&dst))) {
        str_reset(&src);
        str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/ghostty/ghostty-theme");
        if (file_exists(str_text(&src)))
            ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    }

    /* --- WSLg: force the software GSK renderer ------------------------------
     * GTK draws Ghostty's window chrome through GSK, and under WSLg both
     * accelerated GSK backends damage-track the titlebar wrongly, leaving a
     * sliver of the old glyph stranded when the centered title shrinks. Clean on
     * GSK_RENDERER=cairo, the software path.
     *
     * /etc/environment, because it is the only file BOTH launch paths read: the
     * WSLDVCPlugin shortcut runs /usr/bin/ghostty with no shell and no desktop
     * file in the loop, and `ghostty` from the cli sees no desktop entry at all.
     * Distro-global rather than per-app, and that is the honest tradeoff -- what
     * is broken is WSLg's GL stack, not Ghostty. Guarded by OSR_VIRT so a real
     * desktop, where `gl` is both correct and cheaper, never sees it. */
    if (strcmp(env_str("OSR_VIRT", "none"), "wsl") == 0) {
        argv[0] = (char *)"sh"; argv[1] = (char *)"-c";
        argv[2] = (char *)"\n            sed -i \"/^GSK_RENDERER=/d\" /etc/environment\n            printf \"GSK_RENDERER=cairo\\n\" >>/etc/environment\n        ";
        argv[3] = NULL;
        ok = osr_run_step_root("Ghostty: software GSK renderer (WSLg titlebar artifact)",
                               argv) && ok;
    }

    str_free(&src); str_free(&dst); str_free(&out); str_free(&ver);
    return ok;
}
