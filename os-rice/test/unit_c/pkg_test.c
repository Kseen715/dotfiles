/* test/unit_c/pkg_test.c -- what lib/pkg.c must do: resolve a name through the
 * pkgmap, and install it by whichever method the resolved row names.
 *
 * This is the unit distro variance lives in (DESIGN, Core principle), so it is
 * also the one whose defects are widest: a wrong resolution installs the wrong
 * package on one distro and nothing at all on another, and a wrong dispatch
 * runs a package manager as root with arguments nobody asserted.
 *
 * Two things are asserted here, and they are different in kind:
 *
 *   1. THE FACET LADDER -- a pure function from (name, codename, version, arch)
 *      to a row. Asserted against a map this test writes, so the expectations
 *      state the rule rather than restating whatever lib/pkgmap/ happens to
 *      hold today.
 *
 *   2. THE DISPATCH -- what each method actually runs. Asserted as the COMPLETE
 *      argv log of a sandboxed run, because what an install did to a box is the
 *      whole of what it ran: an extra package manager call is as much a defect
 *      as a missing one.
 *
 * Hermetic: $PATH is a directory of stubs, so every package manager, every
 * probe and every AUR helper answers what the scenario says and no real one is
 * reachable; $OSR_LIB points at a map this test wrote; $OSR_APT_BOOTSTRAP_LISTS
 * rebases the apt source-list repair into the sandbox.
 *
 * ON THE PROBE STUBS
 *
 * `dpkg -s`, `pacman -Q`, `apt-mark showhold` and friends are stubbed WITHOUT
 * logging. They are reads -- "does this box already have it" -- and the answer
 * is the scenario's to state, not the run's to decide. What is asserted is the
 * mutations: what got installed, built, removed, escalated.
 *
 * Replaces test/unit/pkg_dispatch.sh, cargo_dispatch.sh, aur_dispatch.sh,
 * portage_dispatch.sh, facet_qualifier.sh, xbps_conflict.sh,
 * apt_sources_conflict.sh and pkg_c_parity.sh -- eight files that drove
 * lib/pkg.sh, the shell tier this replaced, by redefining its functions. See
 * test/harness.h for why the expectations are stated here instead.
 */
#include "../harness.c"

static OsrSandbox sb;

/* --- the map ----------------------------------------------------------
 *
 * write_map -- put a pkgmap in the sandbox and point $OSR_LIB at it. The
 * facet scenarios need a map whose rows exist to state a rule; the shipped
 * lib/pkgmap/ is asserted separately, at the end.
 */
static void write_map(const char *mgr, const char *body) {
    HStr rel, lib;
    hs_init(&rel);
    hs_init(&lib);
    hs_add(&rel, "lib/pkgmap/");
    hs_add(&rel, mgr);
    hs_add(&rel, ".map");
    osr_sb_write(&sb, hs_text(&rel), body, 0644);
    hs_path(&lib, hs_text(&sb.root), "lib");
    osr_sb_env(&sb, "OSR_LIB", hs_text(&lib));
    hs_free(&rel);
    hs_free(&lib);
}

/* use_real_lib -- point $OSR_LIB back at the shipped tree. */
static void use_real_lib(void) {
    osr_sb_env(&sb, "OSR_LIB", hs_text(&sb.osr_lib));
}

/* resolves -- `osr pkg map <name>` under one set of facets. */
static void resolves(const char *codename, const char *version, const char *arch,
                     const char *name, const char *expected, const char *label) {
    osr_sb_env(&sb, "OSR_CODENAME", codename);
    osr_sb_env(&sb, "OSR_VERSION_ID", version);
    osr_sb_env(&sb, "OSR_ARCH", arch);
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "pkg", "map", name, (const char *)NULL);
    osr_assert_out_is(&sb, expected, label);
}

/* at_version -- the same, for the version-only scenarios that make up most of
 * the ladder; codename and arch are empty so only the version can match. */
static void at_version(const char *version, const char *name,
                       const char *expected, const char *label) {
    resolves("", version, "", name, expected, label);
}

/* --- the dispatch -----------------------------------------------------
 *
 * probe -- a stub that answers a question and logs nothing. `code` is what it
 * exits with; `out` is what it prints (empty for none).
 */
static void probe(const char *name, const char *body) {
    osr_sb_stub_body(&sb, name, body);
}

/* loud -- a stub that logs its own argv and succeeds: a mutation, which is
 * exactly what a scenario is asserting. */
static void loud(const char *name) {
    HStr body;
    hs_init(&body);
    hs_add(&body, "printf '");
    hs_add(&body, name);
    hs_add(&body, " %s\\n' \"$*\" >>\"$LOG\"\n");
    osr_sb_stub_body(&sb, name, hs_text(&body));
    hs_free(&body);
}

/* on_manager -- which package manager the next scenarios run against. */
static void on_manager(const char *mgr) {
    osr_sb_env(&sb, "OSR_PKG", mgr);
}

/* install -- `osr pkg install <names...>`, log cleared first. */
static int install(const char *a, const char *b, const char *c, const char *d,
                   const char *e, const char *f) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "pkg", "install", a, b, c, d, e, f,
                           (const char *)NULL);
}

/* --- scenario setup ---------------------------------------------------- */

/* apt_box -- the probes an apt box answers. INSTALLED and HELD are files
 * holding one package name per line, so a scenario states the box's opinion by
 * writing a file rather than by rebuilding a stub. */
static void apt_box(void) {
    probe("dpkg",
        "[ \"$1\" = \"-s\" ] || exit 0\n"
        "grep -qx \"$2\" \"$INSTALLED\" 2>/dev/null\n");
    probe("apt-mark", "cat \"$HELD\" 2>/dev/null\n");
    loud("apt-get");
    on_manager("apt");
}

/* box_says -- rewrite one of the two opinion files. */
static void box_says(const char *which, const char *lines) {
    osr_sb_write(&sb, which, lines, 0644);
}

/* exists -- is there a file at `rel` under the sandbox. The mirror of
 * osr_assert_absent, for the half of a repair that is about what SURVIVED. */
static int exists(const char *rel) {
    HStr full;
    int rc;
    hs_init(&full);
    hs_path(&full, hs_text(&sb.root), rel);
    rc = access(hs_text(&full), F_OK) == 0;
    hs_free(&full);
    return rc;
}

int main(void) {
    HStr p;

    osr_sb_init(&sb);
    hs_init(&p);

    /* The two opinion files, and the environment that names them. */
    osr_sb_write(&sb, "installed", "", 0644);
    osr_sb_write(&sb, "held", "", 0644);
    hs_path(&p, hs_text(&sb.root), "installed");
    osr_sb_env(&sb, "INSTALLED", hs_text(&p));
    hs_path(&p, hs_text(&sb.root), "held");
    osr_sb_env(&sb, "HELD", hs_text(&p));

    /* ================================================================
     * 1. The facet ladder (was facet_qualifier.sh)
     *
     * A row may be qualified `name@facet`, and the most specific qualifier
     * that matches this box wins: codename, then version, then arch, then the
     * bare row. This is the mechanism behind `lsd@jammy` (G6) and the
     * arch-specific rows (G8), and it is a pure function -- so it is asserted
     * against a map written here, whose rows exist only to state the rule.
     * ================================================================ */
    write_map("apt",
        "foo = foo-bare\n"
        "foo@aarch64 = foo-arm\n"
        "foo@22.04 = foo-jammy-ver\n"
        "foo@jammy = source:provide_foo\n"
        "\n"
        "bar = bar-bare\n"
        "bar@3 = bar-major\n"
        "bar@3.21 = bar-minor\n"
        "bar@3.21.3 = bar-point\n"
        "\n"
        "baz = baz-bare\n"
        "baz@3.20 = baz-prefix\n"
        "baz@<=3.22 = baz-old\n"
        "baz@<4 = baz-older\n"
        "\n"
        "qux = qux-bare\n"
        "qux@>24.04 = qux-newer\n"
        "qux@>=2 = qux-new\n");
    on_manager("apt");

    /* --- 1a. the four tiers, most specific first --------------------- */
    resolves("jammy", "22.04", "aarch64", "foo", "source:provide_foo",
             "the codename facet wins over every other qualifier");
    resolves("noble", "22.04", "aarch64", "foo", "foo-jammy-ver",
             "the version facet wins when no codename row matches");
    resolves("noble", "24.04", "aarch64", "foo", "foo-arm",
             "the arch facet wins when neither codename nor version matches");
    resolves("noble", "24.04", "x86_64", "foo", "foo-bare",
             "the bare row is the fallback when no facet matches");
    resolves("", "", "", "foo", "foo-bare",
             "empty facets do not synthesize a spurious 'foo@' key");
    resolves("noble", "24.04", "x86_64", "zsh", "zsh",
             "a name the map does not list passes through unchanged");

    /* --- 1b. dotted version prefixes --------------------------------- */
    /* Alpine reports a patch level (VERSION_ID=3.21.3), so without prefix
     * matching a map would need a row per point release. `name@3.21` covers
     * all of 3.21.x and `name@3` the whole series -- longest prefix first, and
     * always behind an exact key for the full version. */
    at_version("3.21.3", "bar", "bar-point",
               "an exact version key wins over its own prefixes");
    at_version("3.21.9", "bar", "bar-minor",
               "3.21.9 falls back to the 3.21 prefix row");
    at_version("3.21", "bar", "bar-minor",
               "3.21 matches the 3.21 row exactly");
    at_version("3.9.1", "bar", "bar-major",
               "3.9.1 falls past a missing 3.9 row to the 3 prefix");
    at_version("4.1.0", "bar", "bar-bare",
               "4.1.0 matches no prefix row and takes the bare one");
    at_version("3.210", "bar", "bar-major",
               "3.210 is not 3.21: a prefix is components, not characters");

    /* --- 1c. version ranges ------------------------------------------ */
    /* The question a map row usually wants to ask is "is this release old
     * enough to need the fallback", which no exact key can express. */
    at_version("3.21.3", "baz", "baz-old",
               "3.21.3 takes the first matching range row (<=3.22)");
    at_version("3.22", "baz", "baz-old",
               "3.22 satisfies <=3.22: the boundary is inclusive");
    at_version("3.23", "baz", "baz-older",
               "3.23 misses <=3.22 and falls to <4");
    at_version("4.0", "baz", "baz-bare",
               "4.0 satisfies neither range and takes the bare row");
    at_version("3.20.5", "baz", "baz-prefix",
               "a prefix key outranks a range row that also matches");
    at_version("1.9", "qux", "qux-bare",
               "1.9 misses >=2");
    at_version("2", "qux", "qux-new",
               "2 satisfies >=2: the boundary is inclusive");
    at_version("24.04", "qux", "qux-new",
               "24.04 is not > 24.04, so the tighter row is skipped");
    at_version("24.10", "qux", "qux-newer",
               "24.10 > 24.04: a component is a decimal number, not a string");

    /* --- 1d. the comparison itself ----------------------------------- */
    /* Stated through the resolver rather than through a private entry point:
     * a comparison nothing resolves against is not a behaviour this unit
     * owes anyone. `24.04 == 24.4` is the one worth naming -- a string
     * comparison gets it wrong, and Ubuntu's whole version scheme is built
     * on the leading zero. */
    write_map("apt",
        "eq = eq-bare\n"
        "eq@>=24.4 = eq-ge\n"
        "gt = gt-bare\n"
        "gt@>24.4 = gt-gt\n"
        "series = series-bare\n"
        "series@3 = series-hit\n"
        "sp = sp-bare\n"
        "sp@>=15 = sp-hit\n");
    /* A range is where the comparison happens, and it is numeric per
     * component: 24.04 and 24.4 are the same release, which a string
     * comparison gets wrong and Ubuntu's whole version scheme depends on. */
    at_version("24.04", "eq", "eq-ge",
               "24.04 >= 24.4: a leading zero is decimal, not a character");
    at_version("24.04", "gt", "gt-bare",
               "24.04 is not > 24.4: they are the same release");
    at_version("3.0.0", "series", "series-hit",
               "3.0.0 falls to the 3 prefix row: missing components are zero");
    at_version("15-SP5", "sp", "sp-hit",
               "15-SP5 compares as 15: a component keeps its leading digits only");

    /* ================================================================
     * 2. Native dispatch (was pkg_dispatch.sh)
     *
     * Every native row in one call, and the two reasons a package drops out
     * of it before the call is built: it is already installed (SS2), or the
     * user has held it (G2).
     * ================================================================ */
    write_map("apt",
        "build = build-essential\n"
        "starship = script:https://example.invalid/install.sh --yes\n"
        "paru = source:provide_paru\n"
        "serie = cargo:serie\n");
    osr_sb_env(&sb, "OSR_CODENAME", "noble");
    osr_sb_env(&sb, "OSR_VERSION_ID", "24.04");
    osr_sb_env(&sb, "OSR_ARCH", "x86_64");
    apt_box();

    box_says("installed", "curl\n");
    box_says("held", "vim\n");
    install("zsh", "curl", "vim", "build", NULL, NULL);
    osr_assert_log_is(&sb,
        "sudo env DEBIAN_FRONTEND=noninteractive apt-get update -q -o Dpkg::Use-Pty=0\n"
        "apt-get update -q -o Dpkg::Use-Pty=0\n"
        "sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y -q "
        "-o Dpkg::Use-Pty=0 zsh build-essential\n"
        "apt-get install -y -q -o Dpkg::Use-Pty=0 zsh build-essential\n",
        "apt: one refresh, then one install of the rows that are not already "
        "installed or held");
    osr_assert_out(&sb, "curl already installed - skipping",
        "an installed package says why it was skipped");
    osr_assert_err(&sb, "vim is held/pinned - skipping",
        "a held package says why it was skipped, on stderr (G2)");

    /* Rerun with nothing left to do: no package manager runs at all (SS2).
     * The refresh is inside the install path, so it does not fire either. */
    box_says("installed", "curl\nzsh\nbuild-essential\n");
    install("zsh", "curl", "vim", "build", NULL, NULL);
    osr_assert_log_empty(&sb,
        "a rerun with everything already installed runs no package manager (SS2)");
    box_says("installed", "curl\n");

    /* The index is refreshed once per RUN, not once per package: a fresh
     * container has no lists, and every call after the first would be waste. */
    install("zsh", "git", NULL, NULL, NULL, NULL);
    osr_assert_log_is(&sb,
        "sudo env DEBIAN_FRONTEND=noninteractive apt-get update -q -o Dpkg::Use-Pty=0\n"
        "apt-get update -q -o Dpkg::Use-Pty=0\n"
        "sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y -q "
        "-o Dpkg::Use-Pty=0 zsh git\n"
        "apt-get install -y -q -o Dpkg::Use-Pty=0 zsh git\n",
        "two native rows in one install command, behind one refresh");

    /* ================================================================
     * 3. The other native managers
     *
     * Same resolution, same skip rules, a different argv. Each one is
     * idempotent BY ITS OWN FLAGS as well as by the probe above it --
     * --needed, --noreplace -- because a rerun must not rebuild.
     * ================================================================ */
    box_says("installed", "");
    box_says("held", "");

    write_map("pacman", "build = base-devel\n");
    on_manager("pacman");
    probe("pacman",
        "[ \"$1\" = \"-Q\" ] && { grep -qx \"$2\" \"$INSTALLED\" 2>/dev/null; exit $?; }\n"
        "printf 'pacman %s\\n' \"$*\" >>\"$LOG\"\n");
    install("zsh", "build", NULL, NULL, NULL, NULL);
    osr_assert_log_is(&sb,
        "sudo pacman -Sy --noconfirm\n"
        "pacman -Sy --noconfirm\n"
        "sudo pacman -S --needed --noconfirm zsh base-devel\n"
        "pacman -S --needed --noconfirm zsh base-devel\n",
        "pacman: --needed makes the install itself a no-op on a rerun");

    write_map("apk", "build = build-base\n");
    on_manager("apk");
    probe("apk",
        "[ \"$1\" = \"info\" ] && { grep -qx \"$3\" \"$INSTALLED\" 2>/dev/null; exit $?; }\n"
        "printf 'apk %s\\n' \"$*\" >>\"$LOG\"\n");
    install("zsh", "build", NULL, NULL, NULL, NULL);
    osr_assert_log_is(&sb,
        "sudo apk update\n"
        "apk update\n"
        "sudo apk add zsh build-base\n"
        "apk add zsh build-base\n",
        "apk: update then add, batched");

    write_map("dnf", "build = gcc\n");
    on_manager("dnf");
    probe("rpm", "grep -qx \"$3\" \"$INSTALLED\" 2>/dev/null\n");
    loud("dnf");
    install("zsh", "build", NULL, NULL, NULL, NULL);
    osr_assert_log_is(&sb,
        "sudo dnf -q makecache\n"
        "dnf -q makecache\n"
        "sudo dnf install -y zsh gcc\n"
        "dnf install -y zsh gcc\n",
        "dnf: makecache then install, batched");

    /* --- portage (was portage_dispatch.sh) ---------------------------- */
    /* --noreplace is what makes emerge idempotent, and --getbinpkg is what
     * keeps a Gentoo rice from compiling the world; both are asserted because
     * either one going missing turns a rerun into an afternoon. */
    write_map("portage", "build = sys-devel/gcc\ngit = dev-vcs/git\n");
    on_manager("portage");
    probe("qlist", "grep -qx \"$3\" \"$INSTALLED\" 2>/dev/null\n");
    loud("emerge");
    box_says("installed", "dev-vcs/git\n");
    install("zsh", "git", "build", "fastfetch", NULL, NULL);
    osr_assert_log_is(&sb,
        "sudo emerge --sync --quiet\n"
        "emerge --sync --quiet\n"
        "sudo emerge --quiet --noreplace --getbinpkg zsh sys-devel/gcc fastfetch\n"
        "emerge --quiet --noreplace --getbinpkg zsh sys-devel/gcc fastfetch\n",
        "portage: --noreplace and --getbinpkg, with the installed atom filtered out");
    box_says("installed", "");

    /* ================================================================
     * 3b. script: and source: (was pkg_dispatch.sh)
     *
     * Each provider owns its own idempotency probe, which is the whole point
     * of tagging a row with a method: "is this installed" has a different
     * answer for a curl-piped installer, a crate and a native package, and
     * none of them is the native package database.
     *
     * For script: and source: the probe is `command -v <logical name>` -- the
     * installer is supposed to leave a command of that name behind.
     * ================================================================ */
    write_map("apt",
        "starship = script:https://example.invalid/install.sh --yes\n"
        "paru = source:provide_paru\n"
        "build = build-essential libtool pkg-config\n");
    apt_box();
    box_says("installed", "");
    box_says("held", "");
    /* The downloader writes the installer to stdout, which via_script pipes
     * into `sh -s -- <args>`; the sh the script runs under is the sandbox's,
     * so the "installer" here is a script that logs that it ran. */
    probe("curl",
        "printf 'curl %s\\n' \"$*\" >>\"$LOG\"\n"
        "printf 'printf \"installer %%s\\\\n\" \"$*\" >>\"$LOG\"\\n'\n");

    install("starship", NULL, NULL, NULL, NULL, NULL);
    osr_assert_log(&sb, "installer --yes",
        "script: the args after the URL are forwarded to the installer, not dropped");
    osr_assert_out(&sb, "installing starship via script installer",
        "script: the install says which package and by what method");

    /* SS2: the command the installer leaves behind is the probe. */
    osr_sb_write(&sb, "bin/starship", "#!/bin/sh\n", 0755);
    install("starship", NULL, NULL, NULL, NULL, NULL);
    osr_assert_log_empty(&sb,
        "script: nothing is downloaded when the command is already there (SS2)");
    osr_assert_out(&sb, "starship already present (script) - skipping",
        "script: the skip names the method it skipped");
    osr_sb_rm(&sb, "bin/starship");

    /* source: hands the row to the named builder. The probe is the same
     * `command -v`, so a built binary already on the box is never rebuilt --
     * which for a source row is the difference between a rerun and an hour. */
    osr_sb_write(&sb, "bin/paru", "#!/bin/sh\n", 0755);
    install("paru", NULL, NULL, NULL, NULL, NULL);
    osr_assert_log_empty(&sb, "source: a built binary is never rebuilt (SS2)");
    osr_assert_out(&sb, "paru already present (source) - skipping",
        "source: the skip names the method it skipped");
    osr_sb_rm(&sb, "bin/paru");

    /* One logical name, several real packages, still ONE install command:
     * `build` is the row every rice leans on and it expands to a list. */
    install("build", NULL, NULL, NULL, NULL, NULL);
    osr_assert_log_is(&sb,
        "sudo env DEBIAN_FRONTEND=noninteractive apt-get update -q -o Dpkg::Use-Pty=0\n"
        "apt-get update -q -o Dpkg::Use-Pty=0\n"
        "sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y -q "
        "-o Dpkg::Use-Pty=0 build-essential libtool pkg-config\n"
        "apt-get install -y -q -o Dpkg::Use-Pty=0 build-essential libtool pkg-config\n",
        "a one-to-many row expands into one install command, not three");

    /* Each real package in a one-to-many row is probed on its own, so a row
     * that is half-installed installs only the half that is missing. */
    box_says("installed", "libtool\n");
    install("build", NULL, NULL, NULL, NULL, NULL);
    osr_assert_log_is(&sb,
        "sudo env DEBIAN_FRONTEND=noninteractive apt-get update -q -o Dpkg::Use-Pty=0\n"
        "apt-get update -q -o Dpkg::Use-Pty=0\n"
        "sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y -q "
        "-o Dpkg::Use-Pty=0 build-essential pkg-config\n"
        "apt-get install -y -q -o Dpkg::Use-Pty=0 build-essential pkg-config\n",
        "a half-installed row installs only the packages that are missing");
    box_says("installed", "");

    /* An unknown manager: no installer may be invented for it. Fatal, because
     * carrying on would report success for a box nothing was installed on. */
    on_manager("nosuchpkg");
    osr_assert_rc(install("zsh", NULL, NULL, NULL, NULL, NULL), 1,
        "an unknown package manager is fatal, not a silent skip");
    osr_assert_err(&sb, "no native installer for OSR_PKG='nosuchpkg'",
        "an unknown package manager is named in the error");

    /* ================================================================
     * 3c. pkg installed / pkg remove
     *
     * `installed` answers for a logical name through the same resolution, so
     * a caller never has to know which real packages a row expands to.
     * `remove` filters absent packages out rather than passing them down:
     * every native remover errors on an unknown package, which would make a
     * first run fatal for any module that removes a stack it replaces (SS2).
     * ================================================================ */
    write_map("apt", "build = build-essential libtool\n");
    apt_box();

    box_says("installed", "zsh\n");
    osr_assert_rc(osr_sb_run_core(&sb, "pkg", "installed", "zsh",
                                  (const char *)NULL), 0,
        "installed: an installed package answers yes");
    osr_assert_rc(osr_sb_run_core(&sb, "pkg", "installed", "vim",
                                  (const char *)NULL), 1,
        "installed: an absent package answers no");
    box_says("installed", "build-essential\n");
    osr_assert_rc(osr_sb_run_core(&sb, "pkg", "installed", "build",
                                  (const char *)NULL), 1,
        "installed: a one-to-many row is installed only when ALL of it is");
    box_says("installed", "build-essential\nlibtool\n");
    osr_assert_rc(osr_sb_run_core(&sb, "pkg", "installed", "build",
                                  (const char *)NULL), 0,
        "installed: a one-to-many row answers yes once every package is there");

    box_says("installed", "zsh\n");
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "pkg", "remove", "zsh", "vim", (const char *)NULL);
    osr_assert_log_is(&sb,
        "sudo apt-get remove -y zsh\n"
        "apt-get remove -y zsh\n",
        "remove: the absent package is filtered out instead of erroring the run");
    osr_assert_out(&sb, "vim not installed - nothing to remove",
        "remove: an absent package says so rather than failing silently");

    box_says("installed", "");
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "pkg", "remove", "zsh", (const char *)NULL);
    osr_assert_log_empty(&sb,
        "remove: with nothing installed, no package manager runs (SS2)");

    /* ================================================================
     * 4. cargo: (was cargo_dispatch.sh)
     *
     * A crate is not in any package database, so the probe is the binary in
     * ~/.cargo/bin asked AS THE USER -- root cannot assume it can see that
     * home. Everything here runs as OSR_USER for the same reason.
     * ================================================================ */
    write_map("apt", "serie = cargo:serie\n");
    apt_box();
    osr_sb_mkdir(&sb, "home/.cargo/bin");
    osr_sb_write(&sb, "home/.cargo/bin/cargo",
                 "#!/bin/sh\n"
                 "printf 'cargo %s\\n' \"$*\" >>\"$LOG\"\n"
                 "[ \"$1\" = binstall ] && exit ${BINSTALL_RC:-0}\n"
                 "exit 0\n", 0755);

    /* Every probe here goes through `sudo -u`, and that is the behaviour, not
     * noise: root cannot assume it can read the user's home, so asking
     * "is this crate already installed" as root would answer about the wrong
     * home directory. The escalation lines below are how the test says so. */
    install("serie", NULL, NULL, NULL, NULL, NULL);
    osr_assert_log_is(&sb,
        "sudo -u tester test -x ROOT/home/.cargo/bin/serie\n"
        "sudo -u tester test -x ROOT/home/.cargo/bin/cargo\n"
        "sudo -u tester test -x ROOT/home/.cargo/bin/cargo-binstall\n"
        "sudo -u tester ROOT/home/.cargo/bin/cargo install --locked serie\n"
        "cargo install --locked serie\n",
        "cargo: probe the crate, the toolchain and binstall as the user, then "
        "install --locked");

    /* binstall fetches a prebuilt binary where upstream ships one;
     * modules/rust.c installs it, so most boxes have it and most crates never
     * get compiled at all. */
    osr_sb_write(&sb, "home/.cargo/bin/cargo-binstall", "#!/bin/sh\nexit 0\n", 0755);
    install("serie", NULL, NULL, NULL, NULL, NULL);
    osr_assert_log_is(&sb,
        "sudo -u tester test -x ROOT/home/.cargo/bin/serie\n"
        "sudo -u tester test -x ROOT/home/.cargo/bin/cargo\n"
        "sudo -u tester test -x ROOT/home/.cargo/bin/cargo-binstall\n"
        "sudo -u tester ROOT/home/.cargo/bin/cargo binstall --no-confirm serie\n"
        "cargo binstall --no-confirm serie\n",
        "cargo: binstall is preferred to a source build when it is present");
    osr_refute_log(&sb, "install --locked",
        "cargo: nothing is compiled when the prebuilt binary lands");

    /* Not every crate or arch has a prebuilt asset, so a binstall failure has
     * to fall through to the source build rather than end the install. */
    osr_sb_env(&sb, "BINSTALL_RC", "1");
    install("serie", NULL, NULL, NULL, NULL, NULL);
    osr_assert_log_is(&sb,
        "sudo -u tester test -x ROOT/home/.cargo/bin/serie\n"
        "sudo -u tester test -x ROOT/home/.cargo/bin/cargo\n"
        "sudo -u tester test -x ROOT/home/.cargo/bin/cargo-binstall\n"
        "sudo -u tester ROOT/home/.cargo/bin/cargo binstall --no-confirm serie\n"
        "cargo binstall --no-confirm serie\n"
        "sudo -u tester ROOT/home/.cargo/bin/cargo install --locked serie\n"
        "cargo install --locked serie\n",
        "cargo: a failed binstall falls back to the source build");
    osr_assert_err(&sb, "cargo-binstall failed for serie - falling back to a source build",
        "cargo: the fallback says why it happened rather than looking like a retry");
    osr_sb_env(&sb, "BINSTALL_RC", "0");
    osr_sb_rm(&sb, "home/.cargo/bin/cargo-binstall");

    /* SS2: the crate's own binary is the probe, and it is enough on its own --
     * the toolchain is never even looked for. */
    osr_sb_write(&sb, "home/.cargo/bin/serie", "#!/bin/sh\n", 0755);
    install("serie", NULL, NULL, NULL, NULL, NULL);
    osr_assert_log_is(&sb,
        "sudo -u tester test -x ROOT/home/.cargo/bin/serie\n",
        "cargo: an installed crate is one probe and nothing else (SS2)");
    osr_assert_out(&sb, "serie already present (cargo) - skipping",
        "cargo: the skip says which crate and why");
    osr_sb_rm(&sb, "home/.cargo/bin/serie");

    /* Fatal, not a warning: a cargo: row reached with no toolchain is a
     * manifest-order bug (SS4) -- `rust` belongs before it -- and the run
     * cannot install its way out of it. */
    osr_sb_rm(&sb, "home/.cargo/bin/cargo");
    osr_assert_rc(install("serie", NULL, NULL, NULL, NULL, NULL), 1,
        "cargo: no toolchain is fatal, not a skip");
    osr_assert_err(&sb, "install 'rust' before any cargo: package",
        "cargo: the error names the fix, not just the failure");

    /* ================================================================
     * 5. aur: (was aur_dispatch.sh)
     *
     * The probe is `pacman -Q`, not `command -v`: an AUR package registers in
     * the pacman database like a native one, and its binary is routinely named
     * something else entirely (visual-studio-code-insiders-bin -> code-insiders).
     * ================================================================ */
    write_map("pacman",
        "wleave = aur:wleave\n"
        "vscode = aur:visual-studio-code-insiders-bin\n"
        "steam = aur:steam\n");
    on_manager("pacman");
    probe("pacman",
        "[ \"$1\" = \"-Q\" ] && { grep -qx \"$2\" \"$INSTALLED\" 2>/dev/null; exit $?; }\n"
        "printf 'pacman %s\\n' \"$*\" >>\"$LOG\"\n");
    loud("paru");
    box_says("installed", "steam\n");

    install("zsh", "wleave", "steam", "vscode", "discord", NULL);
    osr_assert_log_is(&sb,
        "sudo pacman -Sy --noconfirm\n"
        "pacman -Sy --noconfirm\n"
        "sudo pacman -S --needed --noconfirm zsh discord\n"
        "pacman -S --needed --noconfirm zsh discord\n"
        "sudo -u tester paru -S --needed --noconfirm wleave\n"
        "paru -S --needed --noconfirm wleave\n"
        "sudo -u tester paru -S --needed --noconfirm visual-studio-code-insiders-bin\n"
        "paru -S --needed --noconfirm visual-studio-code-insiders-bin\n",
        "aur: native rows batch together, each AUR row goes through the helper "
        "as the user, and the one already in the pacman database is skipped");
    osr_assert_out(&sb, "steam already installed (aur) - skipping",
        "aur: an installed AUR package says why it was skipped (SS2)");

    /* The helper is resolved at install time rather than during detection,
     * because paru is often BUILT mid-run by an earlier manifest row. */
    osr_sb_rm(&sb, "bin/paru");
    loud("yay");
    box_says("installed", "");
    install("wleave", NULL, NULL, NULL, NULL, NULL);
    osr_assert_log_is(&sb,
        "sudo -u tester yay -S --needed --noconfirm wleave\n"
        "yay -S --needed --noconfirm wleave\n",
        "aur: yay is used when paru is not on the box");

    osr_sb_rm(&sb, "bin/yay");
    install("wleave", NULL, NULL, NULL, NULL, NULL);
    osr_assert_log_empty(&sb, "aur: with no helper, nothing is run");
    osr_assert_err(&sb, "install 'paru' before any aur: package",
        "aur: a missing helper names the fix (SS4)");

    /* ================================================================
     * 6. xbps conflicts (was xbps_conflict.sh)
     *
     * The one place os-rice removes a package the user may have installed,
     * which G2 otherwise forbids -- so it is fenced in tightly.
     *
     * The case it exists for: `unclutter-xfixes` provides the virtual
     * `unclutter>=0`, so on a box carrying the original `unclutter` xbps
     * refuses the WHOLE transaction and the other six packages in the same
     * call never land. xbps itself is the authority on what conflicts: a dry
     * run reports them without touching anything, and DRYRUN below is real
     * xbps output.
     * ================================================================ */
    write_map("xbps", "");
    on_manager("xbps");
    osr_sb_env(&sb, "OSR_CODENAME", "rolling");
    probe("xbps-query",
        "[ \"$1\" = \"-X\" ] && { grep -qx \"$2\" \"$REVDEPS\" 2>/dev/null "
        "&& echo somepkg-1.0_1; exit 0; }\n"
        "grep -qx \"$1\" \"$INSTALLED\" 2>/dev/null\n");
    probe("xbps-uhelper",
        "printf '%s\\n' \"$2\" | sed 's/-[^-]*_[0-9]*$//'\n");
    probe("xbps-install",
        "[ \"$1\" = \"-n\" ] && { cat \"$DRYRUN\"; exit 0; }\n"
        "printf 'xbps-install %s\\n' \"$*\" >>\"$LOG\"\n");
    loud("xbps-remove");
    /* The hold check greps the xbps.d config directories by absolute path, so
     * this is the one probe the sandbox cannot answer with a file: grep itself
     * is stubbed for that one query and hands everything else to the real
     * thing, which the rest of the run still needs. */
    probe("grep",
        "case \"$3\" in\n"
        "  /etc/xbps.d) cat \"$HELD_CONF\" 2>/dev/null; exit 0 ;;\n"
        "esac\n"
        "exec /usr/bin/grep \"$@\"\n");
    osr_sb_write(&sb, "held.conf", "", 0644);
    hs_path(&p, hs_text(&sb.root), "held.conf");
    osr_sb_env(&sb, "HELD_CONF", hs_text(&p));
    osr_sb_write(&sb, "revdeps", "", 0644);
    hs_path(&p, hs_text(&sb.root), "revdeps");
    osr_sb_env(&sb, "REVDEPS", hs_text(&p));
    hs_path(&p, hs_text(&sb.root), "dryrun");
    osr_sb_env(&sb, "DRYRUN", hs_text(&p));

    osr_sb_write(&sb, "dryrun",
        "CONFLICT: unclutter-xfixes-1.6_1 with installed pkg unclutter-8_5 "
        "(matched by unclutter>=0)\n"
        "ERROR: Transaction aborted due to conflicting packages.\n", 0644);
    install("i3", "unclutter-xfixes", "xclip", NULL, NULL, NULL);
    osr_assert_log_is(&sb,
        "sudo xbps-install -S\n"
        "xbps-install -S\n"
        "sudo xbps-install -n i3 unclutter-xfixes xclip\n"
        "sudo xbps-remove -y unclutter\n"
        "xbps-remove -y unclutter\n"
        "sudo xbps-install -y i3 unclutter-xfixes xclip\n"
        "xbps-install -y i3 unclutter-xfixes xclip\n",
        "xbps: a dry run finds the conflict, the INSTALLED side is removed, "
        "then the whole batch installs");
    osr_refute_log(&sb, "xbps-remove -y unclutter-xfixes",
        "xbps: the package being installed is never the one removed");
    osr_assert_err(&sb, "same program, different implementation",
        "xbps: the removal says what it is doing and why");

    osr_sb_write(&sb, "dryrun",
        "i3-4.25.1_1 install x86_64 https://repo-default.voidlinux.org/current 1 1\n",
        0644);
    install("i3", "xclip", NULL, NULL, NULL, NULL);
    osr_refute_log(&sb, "xbps-remove", "xbps: a clean transaction removes nothing");

    /* Two NEW packages disagreeing: there is nothing installed to remove, and
     * no basis here to pick a winner. Left for xbps to report. */
    osr_sb_write(&sb, "dryrun", "CONFLICT: foo-1_1 with bar-2_1 in transaction\n", 0644);
    install("foo", "bar", NULL, NULL, NULL, NULL);
    osr_refute_log(&sb, "xbps-remove",
        "xbps: an in-transaction conflict is left for xbps to report");

    /* G2: a hold is a stated user decision, and it outranks the install.
     *
     * This is the fence around the only removal os-rice performs, and it did
     * not exist until this test was written from the behaviour rather than
     * from lib/pkg.sh: neither tier had an xbps branch in native_held, and the
     * sh test redefined _native_held, so it asserted a fence that was not
     * there. See lib/pkg.c's native_held. */
    osr_sb_write(&sb, "dryrun",
        "CONFLICT: newthing-1_1 with installed pkg pinnedpkg-3_2 "
        "(matched by pinnedpkg>=0)\n", 0644);
    osr_sb_write(&sb, "held.conf", "ignorepkg=pinnedpkg\n", 0644);
    install("newthing", NULL, NULL, NULL, NULL, NULL);
    osr_refute_log(&sb, "xbps-remove", "xbps: a held package is never removed (G2)");
    osr_assert_err(&sb, "pinnedpkg-3_2 is held",
        "xbps: the held package is named rather than silently skipped");
    osr_sb_write(&sb, "held.conf", "", 0644);

    /* Something else depends on it: removing it would cascade, which is not
     * this function's call to make. */
    osr_sb_write(&sb, "dryrun",
        "CONFLICT: newthing-1_1 with installed pkg needed-3_2 (matched by needed>=0)\n",
        0644);
    osr_sb_write(&sb, "revdeps", "needed\n", 0644);
    install("newthing", NULL, NULL, NULL, NULL, NULL);
    osr_refute_log(&sb, "xbps-remove",
        "xbps: a package other packages depend on is left alone");
    osr_assert_err(&sb, "1 package(s) need it",
        "xbps: the reason names how many packages stood in the way");
    osr_sb_write(&sb, "revdeps", "", 0644);
    osr_sb_rm(&sb, "bin/grep");
    osr_sb_real(&sb, "grep");

    /* ================================================================
     * 7. The apt bootstrap-list repair (was apt_sources_conflict.sh)
     *
     * apt 3.0 (Debian 13+) treats one repo described twice with different
     * signed-by values as a FATAL parse error -- not for our list, for the
     * WHOLE source list. So the bootstrap list provide_yandex_browser writes
     * takes every later apt call on the box down once the vendor postinst adds
     * its own. The repair hands the repo over as soon as the vendor describes
     * it, and it has to run BEFORE the first apt call or the run dies on a
     * source list apt cannot parse.
     * ================================================================ */
    write_map("apt", "");
    apt_box();
    osr_sb_env(&sb, "OSR_CODENAME", "trixie");
    hs_path(&p, hs_text(&sb.root), "etc/apt/sources.list.d/yandex-browser.list");
    osr_sb_env(&sb, "OSR_APT_BOOTSTRAP_LISTS", hs_text(&p));

    /* fixture: our bootstrap list, and the vendor's -- which writes a trailing
     * slash on the URI and its own keyring path. That mismatch is the fatal
     * one, and it is why the match below has to be on the URI alone. */
    osr_sb_write(&sb, "etc/apt/sources.list.d/yandex-browser.list",
        "deb [arch=amd64 signed-by=/etc/apt/keyrings/yandex-browser.asc] "
        "https://repo.yandex.ru/yandex-browser/deb stable main\n", 0644);
    osr_sb_write(&sb, "etc/apt/sources.list.d/yandex-browser-stable.list",
        "deb [arch=amd64 signed-by=/usr/share/keyrings/yandex-browser.gpg] "
        "https://repo.yandex.ru/yandex-browser/deb/ stable main\n", 0644);

    install("zsh", NULL, NULL, NULL, NULL, NULL);
    osr_assert_log_is(&sb,
        "sudo rm -f ROOT/etc/apt/sources.list.d/yandex-browser.list\n"
        "sudo env DEBIAN_FRONTEND=noninteractive apt-get update -q -o Dpkg::Use-Pty=0\n"
        "apt-get update -q -o Dpkg::Use-Pty=0\n"
        "sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y -q "
        "-o Dpkg::Use-Pty=0 zsh\n"
        "apt-get install -y -q -o Dpkg::Use-Pty=0 zsh\n",
        "apt: the bootstrap list is dropped BEFORE the first apt call, not after");
    osr_assert_absent(&sb, "etc/apt/sources.list.d/yandex-browser.list",
        "apt: our bootstrap list is the one that goes");
    osr_assert_true(exists("etc/apt/sources.list.d/yandex-browser-stable.list"),
        "apt: the vendor list is left alone -- it owns the repo now");

    /* A rerun has nothing left to prune, and must not say otherwise. */
    install("zsh", NULL, NULL, NULL, NULL, NULL);
    osr_refute_log(&sb, "rm -f", "apt: a rerun with the list already gone prunes nothing");

    /* With no vendor list, ours is the only route to the repo and stays. */
    osr_sb_rm(&sb, "etc");
    osr_sb_write(&sb, "etc/apt/sources.list.d/yandex-browser.list",
        "deb [arch=amd64 signed-by=/etc/apt/keyrings/yandex-browser.asc] "
        "https://repo.yandex.ru/yandex-browser/deb stable main\n", 0644);
    install("zsh", NULL, NULL, NULL, NULL, NULL);
    osr_refute_log(&sb, "rm -f",
        "apt: with nothing to replace it, our bootstrap list is kept");

    /* The vendor may describe the repo in deb822 form instead, where the URI
     * is on a `URIs:` line -- which is why the check is a substring match on
     * the URI rather than a parse of a `deb` line. */
    osr_sb_write(&sb, "etc/apt/sources.list.d/yandex-browser.sources",
        "Types: deb\n"
        "URIs: https://repo.yandex.ru/yandex-browser/deb/\n"
        "Suites: stable\n"
        "Components: main\n"
        "Signed-By: /usr/share/keyrings/yandex-browser.gpg\n", 0644);
    install("zsh", NULL, NULL, NULL, NULL, NULL);
    osr_assert_log(&sb, "rm -f ROOT/etc/apt/sources.list.d/yandex-browser.list",
        "apt: a deb822 .sources file counts as the vendor describing the repo");

    /* Off an apt box the repair is not merely unnecessary, it is meaningless. */
    osr_sb_rm(&sb, "etc");
    osr_sb_write(&sb, "etc/apt/sources.list.d/yandex-browser.list",
        "deb [arch=amd64 signed-by=/etc/apt/keyrings/yandex-browser.asc] "
        "https://repo.yandex.ru/yandex-browser/deb stable main\n", 0644);
    osr_sb_write(&sb, "etc/apt/sources.list.d/yandex-browser-stable.list",
        "deb [arch=amd64 signed-by=/usr/share/keyrings/yandex-browser.gpg] "
        "https://repo.yandex.ru/yandex-browser/deb/ stable main\n", 0644);
    write_map("dnf", "");
    on_manager("dnf");
    loud("dnf");
    probe("rpm", "exit 1\n");
    install("zsh", NULL, NULL, NULL, NULL, NULL);
    osr_refute_log(&sb, "rm -f", "the apt repair is a no-op off an apt box");
    osr_sb_env(&sb, "OSR_APT_BOOTSTRAP_LISTS", "/nonexistent");

    /* ================================================================
     * 8. The shipped maps
     *
     * The rules above are asserted against a map written here, so they say
     * what the resolver does rather than what lib/pkgmap/ happens to hold.
     * These few say the shipped maps are real -- a `build` row every distro
     * needs, and the source: rows that must name a builder that exists.
     * ================================================================ */
    use_real_lib();
    on_manager("portage");
    resolves("", "", "x86_64", "build", "sys-devel/gcc",
             "portage.map resolves the logical `build` to gcc");
    on_manager("apt");
    resolves("noble", "24.04", "x86_64", "build", "build-essential",
             "apt.map resolves the logical `build` to build-essential");
    on_manager("apk");
    resolves("", "3.21", "x86_64", "build", "build-base",
             "apk.map resolves the logical `build` to build-base");
    on_manager("pacman");
    resolves("", "", "x86_64", "build", "base-devel",
             "pacman.map resolves the logical `build` to base-devel");

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
