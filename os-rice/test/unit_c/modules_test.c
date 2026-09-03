/* test/unit_c/modules_test.c -- the module tier: what every module owes,
 * and what the handful with real logic do.
 *
 * There are 120 modules and most of them are one line: install these packages.
 * Writing 120 scenarios for that would be 120 restatements of a pkgmap row, so
 * this file is split in two.
 *
 * FIRST, THE CONTRACTS EVERY MODULE OWES, asserted over ALL of them as
 * properties rather than one at a time. These are the promises that make a
 * rice re-runnable, and they are exactly the ones that rot quietly:
 *
 *   ON A BOX WHERE EVERYTHING IS INSTALLED, A MODULE RUNS NO PACKAGE MANAGER.
 *   That is SS2, and it is what makes `osr install` safe to run twice. A
 *   module that reinstalls unconditionally costs minutes and, on a metered
 *   connection, money.
 *
 *   A MODULE FOR ANOTHER DISTRO DOES NOTHING, QUIETLY AND SUCCESSFULLY. A
 *   rice.list is shared across distros; a module with nothing to do on this
 *   one must not fail the run.
 *
 *   A MODULE NEVER FAILS THE RUN OVER A DECISION IT CANNOT MAKE. No
 *   gnome-shell, no CPU vendor, no display server -- each of those is a reason
 *   to skip and say so, not a reason to stop.
 *
 * SECOND, THE MODULES WITH ACTUAL LOGIC, one scenario each: batching, group
 * membership and service enablement, vendor routing, the config layering.
 *
 * Hermetic: $PATH is a directory of stubs, so "is this installed" and every
 * install command are properties of the scenario. The argv log IS the
 * assertion -- what a module did to a box is the list of commands it ran.
 *
 * Replaces test/unit/module_c_parity.sh, which diffed 120 modules against
 * frozen recordings of the shell modules they replaced. See test/harness.h.
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

/* fresh_home -- a module that writes into $HOME must not find the previous
 * scenario's output already there and skip its own write. */
static void fresh_home(void) {
    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "home");
}

static int run_module(const char *name) {
    fresh_home();
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "module", "run", name, (const char *)NULL);
}

static void ran(const char *needle, const char *label) {
    osr_assert_log(&sb, needle, label);
}
static void did_not(const char *needle, const char *label) {
    osr_refute_log(&sb, needle, label);
}
static void said(const char *needle, const char *label) {
    osr_assert_true(strstr(osr_sb_capture_both(&sb), needle) != NULL, label);
}

/* installed -- what `dpkg -s` reports. "all" or "none". */
static void installed(int yes) {
    osr_sb_stub_body(&sb, "dpkg", yes ? "exit 0\n" : "exit 1\n");
}

/* every_module -- the module names, from the core's own registry plus the
 * tree. Read once; the caller walks it. */
static const char *every_module(void) {
    static HStr held;
    static int ready = 0;
    if (!ready) { hs_init(&held); ready = 1; }
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "module", "list", (const char *)NULL);
    hs_reset(&held);
    hs_add(&held, osr_sb_capture(&sb));
    return hs_text(&held);
}

int main(void) {
    HStr p;

    osr_sb_init(&sb);
    hs_init(&p);

    osr_sb_env(&sb, "OSR_PKG", "apt");
    osr_sb_env(&sb, "OSR_DISTRO", "ubuntu");
    osr_sb_env(&sb, "OSR_ID_LIKE", "debian");
    osr_sb_env(&sb, "OSR_CODENAME", "noble");
    osr_sb_env(&sb, "OSR_VERSION_ID", "24.04");
    osr_sb_env(&sb, "OSR_ARCH", "x86_64");
    osr_sb_env(&sb, "OSR_ARCH_DEB", "amd64");
    osr_sb_env(&sb, "OSR_INIT", "systemd");
    hs_path(&p, hs_text(&sb.osr_root), "..");
    osr_sb_env(&sb, "OSR_DOTFILES", hs_text(&p));

    osr_sb_stub_body(&sb, "apt-get",
        "printf 'apt-get %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    osr_sb_stub_body(&sb, "apt-mark", "exit 0\n");
    osr_sb_stub_body(&sb, "systemctl",
        "printf 'systemctl %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");

    /* ================================================================
     * 1. flameshot -- the shape most modules have
     * ================================================================ */
    installed(0);
    run_module("flameshot");
    ran("apt-get install -y -q -o Dpkg::Use-Pty=0 flameshot maim slop xclip",
        "flameshot: ONE batched apt-get for four packages, not four calls -- "
        "each apt invocation is a lock acquisition and an index read");

    installed(1);
    run_module("flameshot");
    did_not("apt-get install",
        "flameshot: a rerun on an installed box runs no install at all (SS2)");

    /* ================================================================
     * 2. The SS2 sweep, over every module in the tree
     *
     * The single most valuable assertion in this file, and the one no
     * per-module scenario would have made: on a box where every package is
     * already installed, NOTHING may run a package manager. One module that
     * forgot its probe would add minutes to every rerun of every rice it is
     * in, and nothing else would notice.
     * ================================================================ */
    {
        HStr names;
        const char *q;
        HStr offenders;
        int checked = 0;

        installed(1);
        osr_sb_stub_body(&sb, "rpm", "exit 0\n");
        osr_sb_stub_body(&sb, "pacman",
            "[ \"$1\" = \"-Q\" ] && exit 0\n"
            "printf 'pacman %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
        hs_init(&names);
        hs_add(&names, every_module());
        hs_init(&offenders);

        q = hs_text(&names);
        while (*q != '\0') {
            HStr one;
            hs_init(&one);
            while (*q != '\0' && *q != '\n') {
                if (*q != ' ') hs_addc(&one, *q);
                q++;
            }
            if (*q == '\n') q++;
            if (one.len == 0) { hs_free(&one); continue; }
            checked++;
            run_module(hs_text(&one));
            if (strstr(osr_sb_log(&sb), "apt-get install") != NULL ||
                strstr(osr_sb_log(&sb), "pacman -S") != NULL) {
                hs_add(&offenders, " ");
                hs_add(&offenders, hs_text(&one));
            }
            hs_free(&one);
        }
        osr_assert_true(checked > 100,
            "the sweep really covered the tree, not an empty list");
        osr_assert_eq("", hs_text(&offenders),
            "no module installs anything on a box where everything is already "
            "installed (SS2) -- this is what makes `osr install` safe to rerun");
        hs_free(&offenders);
        hs_free(&names);
    }

    /* NOTE on what is deliberately NOT swept here: the exit STATUS of every
     * module. A source builder with no downloader on $PATH fails, and it
     * should -- that is a real failure, not a contract violation, and this
     * sandbox has no curl on purpose. The modules that decline for a reason
     * about the BOX rather than about the tooling are asserted one by one in
     * section 5, where the reason can be named. */

    installed(0);
    osr_sb_stub_body(&sb, "rpm", "exit 1\n");

    /* ================================================================
     * 3. pkgmap resolution reaches the modules
     *
     * A module names a LOGICAL package; the pkgmap turns it into this
     * distro's. `osr module pkgmap` is the same resolver the modules use, and
     * it has to agree with `osr pkg map` -- two ladders that disagree send the
     * compiled path to a different package than the shell path chose.
     * ================================================================ */
    {
        static const char *const names[] = {
            "fd", "build", "dev-headers", "zsh", "btop", "nosuchpackage", NULL
        };
        int i;
        for (i = 0; names[i] != NULL; i++) {
            HStr viapkg, label;
            hs_init(&viapkg);
            osr_sb_reset(&sb);
            osr_sb_run_core(&sb, "pkg", "map", names[i], (const char *)NULL);
            hs_add(&viapkg, osr_sb_capture(&sb));
            osr_sb_reset(&sb);
            osr_sb_run_core(&sb, "module", "pkgmap", names[i],
                            (const char *)NULL);
            hs_init(&label);
            hs_add(&label, "pkgmap: `");
            hs_add(&label, names[i]);
            hs_add(&label, "` resolves the same for a module as for the "
                           "package layer");
            osr_assert_eq(hs_text(&viapkg), osr_sb_capture(&sb), hs_text(&label));
            hs_free(&label);
            hs_free(&viapkg);
        }
    }
    /* And the one that matters most: a name the map REWRITES. `fd` is
     * `fd-find` on apt, and a module that shipped the logical name straight
     * to apt would install nothing and report success. */
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "module", "pkgmap", "fd", (const char *)NULL);
    osr_assert_out_is(&sb, "fd-find",
        "pkgmap: `fd` really is rewritten on apt -- the sweep above would pass "
        "just as well against a resolver that returned its input");

    /* ================================================================
     * 4. docker -- package, group, membership, service
     *
     * The most-steps module in the tree, and every step is one somebody
     * forgets by hand: the group has to exist, the user has to be in it, and
     * the daemon has to be enabled AND started.
     * ================================================================ */
    installed(0);
    osr_sb_stub_body(&sb, "getent", "exit 1\n");                /* no group yet */
    osr_sb_stub_body(&sb, "groupadd",
        "printf 'groupadd %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    osr_sb_stub_body(&sb, "usermod",
        "printf 'usermod %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    osr_sb_stub_body(&sb, "systemctl",
        "case \"$1\" in is-enabled|is-active) exit 1 ;; esac\n"
        "printf 'systemctl %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    run_module("docker");
    ran("groupadd docker", "docker: the group is created");
    ran("usermod -aG docker tester",
        "docker: and the riced user is added to it -- without this every "
        "docker command needs sudo, which is not what anybody wants");
    ran("systemctl enable --now docker",
        "docker: the daemon is enabled AND started in one command -- there is "
        "no window in which it is enabled but not running");

    /* A busybox box has no groupadd; addgroup is the same decision with a
     * different tool, and a module that only knew groupadd would leave the
     * user out of the group on every Alpine install. */
    osr_sb_rm(&sb, "bin/groupadd");
    osr_sb_rm(&sb, "bin/usermod");
    osr_sb_stub_body(&sb, "addgroup",
        "printf 'addgroup %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    osr_sb_stub_body(&sb, "adduser",
        "printf 'adduser %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    run_module("docker");
    ran("addgroup docker",
        "docker: a box with no groupadd uses addgroup instead of skipping the "
        "group entirely");

    /* SS2: everything already done. */
    installed(1);
    osr_sb_stub_body(&sb, "getent", "printf 'docker:x:999:tester\\n'\n");
    osr_sb_stub_body(&sb, "id", "printf 'docker\\n'\n");
    osr_sb_stub_body(&sb, "systemctl",
        "case \"$1\" in is-enabled|is-active) exit 0 ;; esac\n"
        "printf 'systemctl %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    run_module("docker");
    did_not("groupadd", "docker: a rerun does not recreate the group");
    did_not("usermod", "docker: nor re-add the user");
    did_not("systemctl enable", "docker: nor re-enable the daemon (SS2)");
    installed(0);

    /* ================================================================
     * 5. The modules that decline, and say why
     *
     * Each of these is a decision the module cannot make on this box. The
     * contract is the same for all three: run nothing, exit 0, and print the
     * reason -- a silent skip is indistinguishable from a bug.
     * ================================================================ */
    osr_sb_env(&sb, "OSR_CPU_VENDOR", "");
    osr_sb_rm(&sb, "bin/gnome-shell");
    {
        static const struct { const char *mod; const char *why; } declines[] = {
            { "paru",
              "paru: an Arch-only module does nothing on apt" },
            { "cpu-microcodes",
              "cpu-microcodes: with no CPU vendor detected it installs none" },
            { "gpaste",
              "gpaste: with no GNOME Shell it runs nothing -- there is nothing "
              "to be a clipboard manager for, and the extension it would build "
              "is compiled against a Shell major that does not exist here" }
        };
        size_t i;
        for (i = 0; i < sizeof(declines) / sizeof(declines[0]); i++) {
            run_module(declines[i].mod);
            osr_assert_true(osr_sb_log(&sb)[0] == '\0', declines[i].why);
        }
    }
    run_module("cpu-microcodes");
    said("no microcode",
        "cpu-microcodes: and says why it installed none, rather than skipping "
        "silently");
    run_module("gpaste");
    said("gnome-shell",
        "gpaste: and names the thing it could not find");

    /* Given a vendor, it installs that vendor's microcode and ONLY that one.
     * Installing both is not merely wasteful: the wrong one is a firmware
     * blob the CPU will not load, and on some boards it breaks early boot. */
    osr_sb_env(&sb, "OSR_CPU_VENDOR", "AuthenticAMD");
    run_module("cpu-microcodes");
    ran("amd-ucode", "cpu-microcodes: an AMD CPU gets the AMD microcode");
    did_not("intel-ucode",
        "cpu-microcodes: and NOT the Intel one -- the wrong blob is worse than "
        "none");
    osr_sb_env(&sb, "OSR_CPU_VENDOR", "GenuineIntel");
    run_module("cpu-microcodes");
    ran("intel-ucode", "cpu-microcodes: an Intel CPU gets the Intel one");
    did_not("amd-ucode", "cpu-microcodes: and not the AMD one");

    /* Installing the package is only half of it. Microcode is loaded before the
     * root filesystem exists, so a blob that never reaches the initramfs is
     * loaded on no boot at all -- which is how a box ran for years on
     * `microcode: Current revision: 0x2` with intel-ucode installed. */
    osr_sb_stub_body(&sb, "dracut", "printf 'dracut %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    run_module("cpu-microcodes");
    ran("dracut --force",
        "cpu-microcodes: and the initramfs is rebuilt -- the only place the CPU "
        "ever reads a microcode blob from");

    /* Void keeps the Intel blob in a repo that is off by default, so the
     * install used to be a silent no-op there. The module turns that repo on
     * ITSELF, which is a user-visible act and therefore announced and
     * declinable -- rather than a pkgmap row doing it behind the user's back. */
    osr_sb_env(&sb, "OSR_PKG", "xbps");
    osr_sb_env(&sb, "OSR_DISTRO", "void");
    osr_sb_env(&sb, "OSR_ID_LIKE", "");
    /* xbps-query answers "not installed" so both steps actually run; the
     * installer logs its argv like every other manager stub here. */
    osr_sb_stub_body(&sb, "xbps-query", "exit 1\n");
    osr_sb_stub_body(&sb, "xbps-install",
        "printf 'xbps-install %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    run_module("cpu-microcodes");
    ran("void-repo-nonfree",
        "cpu-microcodes: on Void the package layer enables the nonfree repo "
        "first - the module asks for it, it does not own it");
    ran("intel-ucode",
        "cpu-microcodes: so the blob resolves to a package that exists");
    said("nonfree",
        "cpu-microcodes: and the run says it turned a nonfree repo on");

    osr_sb_env(&sb, "OSR_NONFREE", "0");
    run_module("cpu-microcodes");
    did_not("void-repo-nonfree",
        "cpu-microcodes: OSR_NONFREE=0 leaves the repo alone - one switch for "
        "every nonfree package, not a per-module knob");
    did_not("intel-ucode",
        "cpu-microcodes: and installs nothing out of it");
    said("OSR_NONFREE=0",
        "cpu-microcodes: declining is reported, not silent");
    osr_sb_env(&sb, "OSR_NONFREE", "");
    osr_sb_rm(&sb, "bin/dracut");
    osr_sb_rm(&sb, "bin/xbps-install");
    osr_sb_rm(&sb, "bin/xbps-query");
    osr_sb_env(&sb, "OSR_PKG", "apt");
    osr_sb_env(&sb, "OSR_DISTRO", "ubuntu");
    osr_sb_env(&sb, "OSR_ID_LIKE", "debian");
    osr_sb_env(&sb, "OSR_CPU_VENDOR", "");

    /* ================================================================
     * 6. fastfetch -- the three routes a themed config can take
     *
     * fastfetch ships ONE config file, so it is the clearest case of the SS6b
     * precedence: the theme's own file, else the app's template rendered with
     * the theme's palette, else the dotfiles base unrendered.
     * ================================================================ */
    osr_sb_env(&sb, "OSR_PKG", "pacman");
    osr_sb_env(&sb, "OSR_DISTRO", "arch");
    osr_sb_env(&sb, "OSR_ID_LIKE", "");
    osr_sb_env(&sb, "OSR_CODENAME", "");
    osr_sb_env(&sb, "OSR_VERSION_ID", "");
    osr_sb_stub_body(&sb, "pacman",
        "[ \"$1\" = \"-Q\" ] && exit 1\n"
        "printf 'pacman %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    osr_sb_env(&sb, "OSR_DOTFILES", at("df"));
    osr_sb_env(&sb, "OSR_THEME", "nord");
    osr_sb_env(&sb, "OSR_THEME_DIR", at("themes/nord"));
    {
        HStr src;
        char *palette;
        hs_init(&src);
        hs_path(&src, hs_text(&sb.osr_root), "themes/nord/theme.list");
        palette = h_slurp(hs_text(&src));
        osr_sb_write(&sb, "themes/nord/theme.list", palette, 0644);
        free(palette);
        hs_free(&src);
    }

    /* (a) the theme ships the file itself */
    osr_sb_write(&sb, "themes/nord/config/fastfetch/config.jsonc",
                 "THEME OWNED\n", 0644);
    osr_sb_write(&sb, "df/fastfetch/config.jsonc", "DOTFILES BASE\n", 0644);
    run_module("fastfetch");
    {
        char *got = h_slurp(at("home/.config/fastfetch/config.jsonc"));
        osr_assert_eq("THEME OWNED\n", got,
            "fastfetch: a file the theme ships itself wins outright");
        free(got);
    }

    /* (b) no theme file: the template, rendered */
    osr_sb_rm(&sb, "themes/nord/config/fastfetch/config.jsonc");
    osr_sb_write(&sb, "df/fastfetch/config.jsonc.tmpl",
        "bg={{background}} name={{THEME}}\n"
        "missing={{nosuchkey}} wall={{WALLPAPER_PATH}}\n", 0644);
    run_module("fastfetch");
    {
        char *got = h_slurp(at("home/.config/fastfetch/config.jsonc"));
        osr_assert_true(strstr(got, "name=nord") != NULL,
            "fastfetch: with no theme file the template is rendered");
        osr_assert_true(strstr(got, "bg=#") != NULL,
            "fastfetch: with the theme's own palette substituted in");
        osr_assert_true(strstr(got, "missing={{nosuchkey}}") != NULL,
            "fastfetch: a key the theme does not define is left VISIBLE, so "
            "the gap is obvious rather than a silently empty value (SS9)");
        osr_assert_true(strstr(got, "wall={{WALLPAPER_PATH}}") != NULL,
            "fastfetch: {{WALLPAPER_PATH}} survives this pass -- it is "
            "resolved later, once the wallpaper has actually been installed");
        free(got);
    }
    said("defines no nosuchkey",
        "fastfetch: and the undefined key is warned about BY NAME -- even "
        "though it shares its line with the deferred {{WALLPAPER_PATH}}, which "
        "is skipped on its own rather than taking the whole line with it");

    /* (c) neither: the dotfiles base, unrendered */
    osr_sb_rm(&sb, "df/fastfetch/config.jsonc.tmpl");
    run_module("fastfetch");
    {
        char *got = h_slurp(at("home/.config/fastfetch/config.jsonc"));
        osr_assert_eq("DOTFILES BASE\n", got,
            "fastfetch: with neither a theme file nor a template, the dotfiles "
            "base lands -- an app is never left with no config at all");
        free(got);
    }

    /* ================================================================
     * 7. helpers -- seeded once, then the user's
     *
     * The first module written in C from scratch rather than ported. What
     * makes it a module at all is the two files it seeds and the two
     * DIFFERENT identities they are written under.
     * ================================================================ */
    osr_sb_env(&sb, "OSR_PKG", "apt");
    osr_sb_env(&sb, "OSR_DISTRO", "ubuntu");
    osr_sb_env(&sb, "OSR_ID_LIKE", "debian");
    osr_sb_env(&sb, "OSR_CODENAME", "noble");
    osr_sb_env(&sb, "OSR_VERSION_ID", "24.04");
    hs_path(&p, hs_text(&sb.osr_root), "..");
    osr_sb_env(&sb, "OSR_DOTFILES", hs_text(&p));
    installed(0);
    run_module("helpers");
    ran("apt-get install -y -q -o Dpkg::Use-Pty=0 exo-utils xterm",
        "helpers: `exo` maps to exo-utils on apt, with xterm as the terminal "
        "of last resort");
    {
        char *rc = h_slurp(at("home/.config/xfce4/helpers.rc"));
        osr_assert_true(strstr(rc, "TerminalEmulator=osr-term") != NULL,
            "helpers: the terminal role resolves to the session's own launcher, "
            "so `exo-open --launch TerminalEmulator` opens the riced terminal");
        osr_assert_true(strstr(rc, "FileManager=Thunar") != NULL,
            "helpers: and the file-manager role is set too");
        free(rc);
    }
    ran("tee /usr/share/xfce4/helpers/osr-term.desktop",
        "helpers: the system helper entry is written under /usr/share");
    ran("sudo",
        "helpers: escalating to do it -- unlike the file in $HOME");
    did_not("sudo -u tester tee /usr/share",
        "helpers: and the system file is NOT written as the riced user, who "
        "has no business owning something under /usr/share");

    /* SS5: seeded, then the user's. A rerun rewrites neither. */
    osr_sb_write(&sb, "home/.config/xfce4/helpers.rc",
                 "TerminalEmulator=xterm\n", 0644);
    installed(1);
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "module", "run", "helpers", (const char *)NULL);
    did_not("apt-get install", "helpers: a rerun installs nothing");
    {
        char *rc = h_slurp(at("home/.config/xfce4/helpers.rc"));
        osr_assert_eq("TerminalEmulator=xterm\n", rc,
            "helpers: and leaves an edited helpers.rc exactly as the user left "
            "it -- that is the whole point of seeding rather than installing");
        free(rc);
    }

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
