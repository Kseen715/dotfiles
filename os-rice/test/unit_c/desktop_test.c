/* test/unit_c/desktop_test.c -- the whole-desktop modules, the front end, and
 * what a rice switch is allowed to touch.
 *
 * A desktop module is the biggest thing a rice installs: a window manager plus
 * the dozen small programs its config execs. Two properties matter, and they
 * pull in opposite directions:
 *
 *   EVERYTHING THE CONFIG REFERS TO IS INSTALLED. An i3 config that binds a
 *   key to a script that is not there produces a keypress that does nothing,
 *   with no error anywhere.
 *
 *   A RICE SWITCH TOUCHES ONLY WHAT THE RICE OWNS. The theme layer swaps; the
 *   user's 00-env.zsh and 99-local.zsh do not; the previous rice's wallpaper
 *   FILE stays on disk and only the pointer moves. Switching rices is
 *   something people do to try one out, and it has to be safe to do twice.
 *
 * The front-end section is here for a different reason. `osr` is a dispatcher,
 * so what can rot in it is a PATH -- and nothing else checks any of them. That
 * is not hypothetical: `osr theme` with no name sourced lib/state.sh for years
 * after lib/state.c replaced it and the file was deleted, and the verb had
 * been dead the whole time.
 *
 * Replaces test/unit/i3_desktop.sh, hyprland_session.sh, switch_asymmetry.sh,
 * front_end.sh and path_guard.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

static const char *at(const char *rel) {
    static HStr ring[4];
    static int ready = 0;
    static int next = 0;
    HStr *p;
    if (!ready) { int i; for (i = 0; i < 4; i++) hs_init(&ring[i]); ready = 1; }
    p = &ring[next];
    next = (next + 1) % 4;
    hs_path(p, hs_text(&sb.root), rel);
    return hs_text(p);
}

static char *read_rel(const char *rel) { return h_slurp(at(rel)); }

static void holds(const char *rel, const char *needle, const char *label) {
    char *got = read_rel(rel);
    osr_assert_true(strstr(got, needle) != NULL, label);
    free(got);
}
static void lacks(const char *rel, const char *needle, const char *label) {
    char *got = read_rel(rel);
    osr_assert_true(strstr(got, needle) == NULL, label);
    free(got);
}
static void exists_x(const char *rel, const char *label) {
    osr_assert_true(access(at(rel), X_OK) == 0, label);
}
static void ran(const char *needle, const char *label) {
    osr_assert_log(&sb, needle, label);
}
static void did_not(const char *needle, const char *label) {
    osr_refute_log(&sb, needle, label);
}

static void run_module(const char *name) {
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "module", "run", name, (const char *)NULL);
}

/* osr_front -- the `osr` front end itself, as a user would type it. */
static int osr_front(const char *a, const char *b) {
    HStr script;
    int rc;
    hs_init(&script);
    hs_path(&script, hs_text(&sb.osr_root), "osr");
    osr_sb_reset(&sb);
    {
        char *argv[6];
        int n = 0;
        argv[n++] = (char *)"/bin/sh";
        argv[n++] = (char *)hs_text(&script);
        if (a != NULL) argv[n++] = (char *)a;
        if (b != NULL) argv[n++] = (char *)b;
        argv[n] = NULL;
        rc = h_run(&sb, argv);
    }
    hs_free(&script);
    return rc;
}

int main(void) {
    HStr p;

    osr_sb_init(&sb);
    hs_init(&p);

    hs_path(&p, hs_text(&sb.osr_root), "..");
    osr_sb_env(&sb, "OSR_DOTFILES", hs_text(&p));
    osr_sb_env(&sb, "OSR_INIT", "systemd");
    osr_sb_stub_body(&sb, "dpkg", "exit 1\n");
    osr_sb_stub_body(&sb, "apt-mark", "exit 0\n");
    osr_sb_stub_body(&sb, "apt-get",
        "printf 'apt-get %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    osr_sb_stub_body(&sb, "pacman",
        "case \"$1\" in -Q*) exit 1 ;; esac\n"
        "printf 'pacman %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    osr_sb_stub_body(&sb, "systemctl",
        "printf 'systemctl %s\\n' \"$*\" >>\"$LOG\"\nexit 1\n");

    /* The `source:` rows a desktop pulls in -- autotiling, the lock screen --
     * are already present, so their builders' own probes short-circuit and
     * nothing here reaches the network. What is under test is the desktop's
     * configuration, not a Python build. */
    {
        static const char *const present[] = {
            "autotiling", "betterlockscreen", "i3lock", "xidlehook",
            "xautolock", "rofi", "picom", "polybar", "hyprland", "Hyprland",
            "waybar", "mako", "wleave", NULL
        };
        int i;
        for (i = 0; present[i] != NULL; i++)
            osr_sb_stub_body(&sb, present[i], "exit 0\n");
    }
    /* The JSON and TOML composers are python3's. */
    osr_sb_real(&sb, "python3");

    /* A desktop module writes into real system paths -- /usr/local/bin for the
     * terminal launcher, /usr/share/wayland-sessions for the session entries --
     * and those have no environment override. So the ESCALATION is what gets
     * redirected: sudo logs the real command and then performs it against a
     * sandbox root. The argv in the log is still exactly what would have run on
     * a real box, which is what the assertions read; $SYSROOT is only how the
     * test gets to look at what landed. */
    osr_sb_env(&sb, "SYSROOT", at("sys"));
    osr_sb_stub_body(&sb, "sudo",
        "printf 'sudo %s\\n' \"$*\" >>\"$LOG\"\n"
        /* Only a ROOT escalation is redirected. `sudo -u <user>` writes into
         * the sandbox home, which is a real directory the module is supposed
         * to create -- rebasing those would move the user's whole config into
         * the fake system root. */
        "_root=1\n"
        "if [ \"$1\" = \"-u\" ]; then _root=0; shift 2; fi\n"
        "_verb=$1\n"
        "if [ \"$_root\" = 0 ]; then exec \"$@\"; fi\n"
        "case \"$_verb\" in\n"
        "  mkdir|install|cp|ln|tee) ;;\n"
        "  *) exec \"$@\" ;;\n"
        "esac\n"
        "shift\n"
        /* Drop the flags, keep the operands: `install -m 0755 src dst` and
         * `ln -sf target link` both reduce to their two paths. */
        "_ops=\n"
        "while [ $# -gt 0 ]; do\n"
        "  case \"$1\" in\n"
        "    -m|-o|-g) shift 2; continue ;;\n"
        "    -*) shift; continue ;;\n"
        "  esac\n"
        "  _ops=\"$_ops $1\"; shift\n"
        "done\n"
        "set -- $_ops\n"
        "_dst=; for _a in \"$@\"; do _dst=$_a; done\n"
        "case \"$_dst\" in /*) ;; *) exit 0 ;; esac\n"
        "case \"$_verb\" in\n"
        "  mkdir) mkdir -p \"$SYSROOT$_dst\"; exit 0 ;;\n"
        "  tee)   mkdir -p \"$SYSROOT$(dirname \"$_dst\")\"; cat >\"$SYSROOT$_dst\"; exit 0 ;;\n"
        "esac\n"
        "_src=; _n=$#; _i=1\n"
        "for _a in \"$@\"; do [ \"$_i\" -eq $((_n - 1)) ] && _src=$_a; _i=$((_i + 1)); done\n"
        "mkdir -p \"$SYSROOT$(dirname \"$_dst\")\"\n"
        "case \"$_verb\" in\n"
        "  ln) ln -sf \"$_src\" \"$SYSROOT$_dst\" 2>/dev/null ;;\n"
        "  *)  cp -f \"$_src\" \"$SYSROOT$_dst\" 2>/dev/null ;;\n"
        "esac\n"
        "exit 0\n");

    /* ================================================================
     * 1. i3 -- a whole X11 desktop
     * ================================================================ */
    osr_sb_env(&sb, "OSR_PKG", "apt");
    osr_sb_env(&sb, "OSR_DISTRO", "ubuntu");
    osr_sb_env(&sb, "OSR_ID_LIKE", "debian");
    osr_sb_env(&sb, "OSR_CODENAME", "noble");
    osr_sb_env(&sb, "OSR_VERSION_ID", "24.04");
    osr_sb_env(&sb, "OSR_THEME", "rosemary");
    hs_path(&p, hs_text(&sb.osr_root), "themes/rosemary");
    osr_sb_env(&sb, "OSR_THEME_DIR", hs_text(&p));
    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "home");
    run_module("i3");

    ran("i3",   "i3: the window manager is installed");
    ran("i3status",
        "i3: and i3status, which the shipped config's bar block execs -- a bar "
        "with no status command is an empty grey strip");
    ran("dex",
        "i3: and dex, which the config uses to start XDG autostart entries");
    ran("unclutter-xfixes",
        "i3: the xfixes unclutter, not the original -- the original polls, and "
        "on a modern X server it hides the pointer over the wrong window");

    holds("home/.config/i3/config", "include ~/.config/i3/config.d/*.conf",
        "i3: the base config includes a drop-in directory, which is what makes "
        "the theme layer and the user's own layer separable at all");
    holds("home/.config/i3/config.d/90-theme.conf", "client.focused",
        "i3: the theme layer carries the window border colours");
    osr_assert_true(access(at("home/.config/i3/config.d/99-local.conf"), F_OK) == 0,
        "i3: and a 99-local layer is seeded empty, so the user has an obvious "
        "place to put their own bindings that a rice switch will not touch");

    exists_x("home/.config/i3/scripts/osd.sh",
        "i3: the on-screen-display script is installed AND executable -- a "
        "binding to a non-executable script is a key that silently does nothing");
    holds("home/.config/i3/config", "osd.sh volume-up",
        "i3: the volume keys are bound to it");
    holds("home/.config/i3/config", "osd.sh light-up",
        "i3: and the brightness keys");
    holds("home/.config/i3/scripts/osd.sh", "pactl",
        "i3: and the script really drives the mixer, rather than being a stub");

    holds("home/.config/i3/config", "xidlehook --not-when-audio",
        "i3: the idle locker does not fire while audio is playing -- locking "
        "the screen in the middle of a call is the bug this prevents");
    holds("home/.config/i3/config", "xautolock -time 10",
        "i3: and there is a fallback locker with an explicit timeout");

    /* ================================================================
     * 2. Hyprland -- a Wayland desktop, and the VM special case
     * ================================================================ */
    osr_sb_env(&sb, "OSR_PKG", "pacman");
    osr_sb_env(&sb, "OSR_DISTRO", "arch");
    osr_sb_env(&sb, "OSR_ID_LIKE", "");
    osr_sb_env(&sb, "OSR_CODENAME", "");
    osr_sb_env(&sb, "OSR_VERSION_ID", "");
    hs_path(&p, hs_text(&sb.root), "theme");
    osr_sb_env(&sb, "OSR_THEME_DIR", hs_text(&p));
    osr_sb_env(&sb, "OSR_THEME", "glass");

    /* The rice's Hyprland tree: the config, the exec-once scripts it starts,
     * the Qt theme, and the two session entries. */
    osr_sb_write(&sb, "theme/config/hypr/hyprland.conf",
        "# RICE-HYPR-MARKER\nmonitor=,preferred,auto,1\n", 0644);
    {
        static const char *const scripts[] = {
            "start-audio", "start-amnezia-vpn-client", "start-mako",
            "start-easyeffects", "start-top", "start-wleave", NULL
        };
        int i;
        for (i = 0; scripts[i] != NULL; i++) {
            HStr rel;
            hs_init(&rel);
            hs_add(&rel, "theme/config/hypr/");
            hs_add(&rel, scripts[i]);
            hs_add(&rel, ".sh");
            osr_sb_write(&sb, hs_text(&rel), "#!/bin/sh\n", 0644);
            hs_free(&rel);
        }
    }
    osr_sb_write(&sb, "theme/config/qt6ct/qt6ct.conf", "qt6ct\n", 0644);
    osr_sb_write(&sb, "theme/config/wayland-sessions/hyprland.desktop",
        "Exec=/usr/share/wayland-sessions/start-hyprland.sh\n", 0644);
    osr_sb_write(&sb, "theme/config/wayland-sessions/start-hyprland.sh",
        "exec Hyprland\n", 0644);
    osr_sb_write(&sb, "theme/config/wayland-sessions/hyprland-vmware.desktop",
        "Name=Hyprland (VMware)\n", 0644);
    osr_sb_write(&sb, "theme/config/wayland-sessions/start-hyprland-vmware.sh",
        "export GSK_RENDERER=cairo\nexec Hyprland\n", 0644);

    osr_sb_env(&sb, "OSR_VIRT", "none");
    osr_sb_rm(&sb, "home");
    osr_sb_rm(&sb, "sys");
    osr_sb_mkdir(&sb, "home");
    run_module("hyprland");

    ran("hyprland", "hyprland: the compositor is installed");
    holds("home/.config/hypr/hyprland.conf", "RICE-HYPR-MARKER",
        "hyprland: the rice's own config is what lands");
    {
        static const char *const scripts[] = {
            "start-audio", "start-amnezia-vpn-client", "start-mako",
            "start-easyeffects", "start-top", "start-wleave", NULL
        };
        int i;
        int all_x = 1;
        for (i = 0; scripts[i] != NULL; i++) {
            HStr rel;
            hs_init(&rel);
            hs_add(&rel, "home/.config/hypr/");
            hs_add(&rel, scripts[i]);
            hs_add(&rel, ".sh");
            if (access(at(hs_text(&rel)), X_OK) != 0) all_x = 0;
            hs_free(&rel);
        }
        osr_assert_true(all_x,
            "hyprland: every exec-once script the config starts is installed "
            "AND executable -- Hyprland reports nothing when one is not, it "
            "simply does not start");
    }
    osr_assert_true(access(at("home/.config/qt6ct/qt6ct.conf"), F_OK) == 0,
        "hyprland: the Qt theme lands too, so Qt applications are not the one "
        "unthemed thing on the desktop");
    osr_assert_true(
        access(at("sys/usr/share/wayland-sessions/start-hyprland.sh"), F_OK) == 0,
        "hyprland: the session launcher goes into the SYSTEM path, because the "
        "display manager reads it before any user session exists");
    osr_assert_absent(&sb, "sys/usr/share/wayland-sessions/hyprland-vmware.desktop",
        "hyprland: and no VMware session entry on bare metal -- offering a "
        "session that forces software rendering would be a trap");

    osr_sb_env(&sb, "OSR_VIRT", "vmware");
    osr_sb_rm(&sb, "home");
    osr_sb_rm(&sb, "sys");
    osr_sb_mkdir(&sb, "home");
    run_module("hyprland");
    osr_assert_true(
        access(at("sys/usr/share/wayland-sessions/hyprland-vmware.desktop"), F_OK) == 0,
        "hyprland: under VMware a second session entry IS offered -- its "
        "launcher forces the cairo renderer, without which Hyprland does not "
        "start on VMware's GPU at all");
    did_not("chown",
        "hyprland: the session launchers are NOT chowned to the user -- they "
        "are read by the display manager before any user session exists, and "
        "an earlier version chowned them \"so sddm can run it\", which sddm "
        "never needed");
    osr_sb_env(&sb, "OSR_VIRT", "none");

    /* ================================================================
     * 3. A rice switch -- what moves and what does not
     * ================================================================ */
    osr_sb_env(&sb, "OSR_PKG", "apt");
    osr_sb_env(&sb, "OSR_DISTRO", "ubuntu");
    osr_sb_env(&sb, "OSR_ID_LIKE", "debian");
    osr_sb_env(&sb, "OSR_CODENAME", "noble");
    osr_sb_env(&sb, "OSR_VERSION_ID", "24.04");
    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "home");
    osr_sb_stub_body(&sb, "zsh", "exit 0\n");
    osr_sb_stub_body(&sb, "git", "exit 0\n");
    osr_sb_stub_body(&sb, "fzf", "printf '0.74.3 (abc)\\n'\n");
    /* Likewise an lsd that starts: zsh.c falls back to the release tarball for
     * one that does not, and the switch under test here is about config
     * layers, not about fetching binaries. */
    osr_sb_stub_body(&sb, "lsd", "printf 'lsd 1.2.0\\n'\n");
    /* starship is already on the box, so the module composes its config
     * instead of fetching an installer -- which is the half under test. */
    osr_sb_stub_body(&sb, "starship", "printf 'starship 1.20.0\\n'\n");

    /* Apply one rice, then let the user write in their own two layers. */
    osr_sb_env(&sb, "OSR_THEME", "gruvbox");
    hs_path(&p, hs_text(&sb.osr_root), "themes/gruvbox");
    osr_sb_env(&sb, "OSR_THEME_DIR", hs_text(&p));
    run_module("zsh");
    run_module("starship");
    {
        char *env0 = read_rel("home/.config/osr/zsh/rc.d/00-env.zsh");
        HStr edited;
        hs_init(&edited);
        hs_add(&edited, env0);
        hs_add(&edited, "export MY_MACHINE_VAR=1\n");
        osr_sb_write(&sb, "home/.config/osr/zsh/rc.d/00-env.zsh",
                     hs_text(&edited), 0644);
        hs_free(&edited);
        free(env0);
    }
    osr_sb_write(&sb, "home/.config/osr/zsh/rc.d/99-local.zsh",
                 "alias mine=\"echo local\"\n", 0644);

    /* ...then switch. */
    osr_sb_env(&sb, "OSR_THEME", "nord");
    hs_path(&p, hs_text(&sb.osr_root), "themes/nord");
    osr_sb_env(&sb, "OSR_THEME_DIR", hs_text(&p));
    run_module("zsh");
    run_module("starship");

    holds("home/.config/osr/zsh/rc.d/90-theme.zsh", "nord",
        "switch: the theme layer swapped to the new rice");
    holds("home/.config/osr/zsh/rc.d/00-env.zsh", "MY_MACHINE_VAR",
        "switch: 00-env.zsh is untouched -- it is the user's, seeded once and "
        "never rewritten (SS5)");
    holds("home/.config/osr/zsh/rc.d/99-local.zsh", "alias mine",
        "switch: and so is 99-local.zsh");
    holds("home/.config/osr/zsh/rc.d/20-aliases.zsh", "lsd",
        "switch: while the dotfiles-owned layer is still there, rewritten");

    holds("home/.config/starship.toml", "88c0d0",
        "switch: starship's palette swapped to the new theme's colours");
    holds("home/.config/starship.toml", "palette = \"theme\"",
        "switch: with the base body composed in around it");
    lacks("home/.config/starship.toml", "fabd2f",
        "switch: and the previous rice's accent is GONE -- only the palette "
        "table swaps, and a leftover duplicate would be a TOML parse error");

    /* ================================================================
     * 4. The front end
     *
     * Read-only verbs only: the listings and the two questions. Nothing here
     * installs, paints or escalates.
     * ================================================================ */
    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "home/.config/osr");

    {
        static const char *const listings[] = { "list", "themes", "modules", NULL };
        int i;
        for (i = 0; listings[i] != NULL; i++) {
            HStr label;
            osr_assert_rc(osr_front(listings[i], NULL), 0,
                          listings[i][0] == 'l' ? "osr list exits 0"
                        : listings[i][0] == 't' ? "osr themes exits 0"
                                                : "osr modules exits 0");
            hs_init(&label);
            hs_add(&label, "osr ");
            hs_add(&label, listings[i]);
            hs_add(&label, " lists something rather than printing nothing");
            osr_assert_true(osr_sb_capture(&sb)[0] != '\0', hs_text(&label));
            hs_free(&label);
        }
    }

    osr_front("theme", NULL);
    osr_assert_out(&sb, "(none applied)",
        "osr theme with nothing applied says so rather than printing a blank");

    osr_sb_write(&sb, "home/.config/osr/state", "theme=nord\n", 0644);
    osr_front("theme", NULL);
    osr_assert_out(&sb, "nord",
        "osr theme with a theme recorded prints it -- this is the verb that "
        "silently sourced a deleted lib/state.sh for years");

    /* An OPTION is still the question, not a theme name: `osr theme
     * --no-reload` must not try to apply a theme called "--no-reload". */
    osr_front("theme", "--no-reload");
    osr_assert_out(&sb, "nord",
        "osr theme <option> is still the question, not an apply of a theme "
        "named after the option");

    osr_assert_rc(osr_front(NULL, NULL), 0, "osr with no verb exits 0");
    osr_assert_out(&sb, "osr install", "and prints its usage");
    osr_assert_rc(osr_front("-h", NULL), 0, "osr -h exits 0");
    osr_assert_out(&sb, "osr install", "and prints the same usage");

    osr_assert_rc(osr_front("no-such-verb", NULL), 1,
        "an unknown verb exits 1");
    osr_assert_true(strstr(osr_sb_capture_both(&sb), "unknown command") != NULL,
        "and says so rather than falling through to something else");

    /* Every verb the usage block documents has a real arm in the dispatcher.
     * A verb listed in the header and missing from the case is the same class
     * of bug as the dead `osr theme` above -- documentation that does not run. */
    {
        static const char *const verbs[] = {
            "install", "switch", "theme", "wallpaper", "module", "list",
            "themes", "modules", "benchmark", "undervolt", "test", NULL
        };
        HStr path;
        char *front;
        int i;
        int missing = 0;

        hs_init(&path);
        hs_path(&path, hs_text(&sb.osr_root), "osr");
        front = h_slurp(hs_text(&path));
        for (i = 0; verbs[i] != NULL; i++) {
            HStr a, b, c;
            hs_init(&a); hs_init(&b); hs_init(&c);
            hs_add(&a, "\n    "); hs_add(&a, verbs[i]); hs_add(&a, ")");
            hs_add(&b, "\n    "); hs_add(&b, verbs[i]); hs_add(&b, "|");
            hs_add(&c, "|"); hs_add(&c, verbs[i]); hs_add(&c, ")");
            if (strstr(front, hs_text(&a)) == NULL &&
                strstr(front, hs_text(&b)) == NULL &&
                strstr(front, hs_text(&c)) == NULL) missing = 1;
            hs_free(&a); hs_free(&b); hs_free(&c);
        }
        free(front);
        hs_free(&path);
        osr_assert_true(!missing,
            "every verb the usage block documents has an arm in the "
            "dispatcher -- a documented verb with no arm is a promise the "
            "front end does not keep");
    }

    /* ================================================================
     * 5. The PATH guard (SS5's rerun contract, for the environment)
     *
     * 00-env.zsh is sourced on every shell, and a login shell can source it
     * more than once. A PATH edit that appends unconditionally turns into a
     * $PATH with the same directory in it a dozen times, which is slow and,
     * on a WSL box with the Windows PATH interpolated, genuinely painful.
     * ================================================================ */
    {
        HStr script;
        char *argv[4];
        hs_init(&script);
        hs_add(&script,
            "PATH=/usr/bin:/bin; export PATH\n"
            "mkdir -p \"$HOME/.cargo/bin\"\n"
            ". \"$OSR_DOTFILES/zsh/rc.d/00-env.zsh\"\n"
            ". \"$OSR_DOTFILES/zsh/rc.d/00-env.zsh\"\n"
            "printf '%s' \"$PATH\" | tr ':' '\\n' | grep -cx \"$HOME/.cargo/bin\"\n");
        osr_sb_rm(&sb, "home");
        osr_sb_mkdir(&sb, "home");
        osr_sb_reset(&sb);
        argv[0] = (char *)"/bin/sh";
        argv[1] = (char *)"-c";
        argv[2] = (char *)hs_text(&script);
        argv[3] = NULL;
        h_run(&sb, argv);
        osr_assert_out_is(&sb, "1\n",
            "sourcing 00-env.zsh twice leaves each PATH entry exactly once -- "
            "the guard is what makes the environment layer re-runnable");
        hs_free(&script);
    }

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
