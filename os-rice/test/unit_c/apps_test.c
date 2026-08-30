/* test/unit_c/apps_test.c -- the applications os-rice installs from a vendor
 * tarball, and the two that reconfigure something the distro already gave you.
 *
 * These are the modules with the least in common with each other, which is why
 * they share a file: each is one vendor's idea of how to ship software, and
 * what is asserted is that os-rice turns that into the same shape every time.
 *
 *   A VENDOR TARBALL (DataGrip, Telegram) is a whole self-contained tree.
 *   Unpacking it under /opt IS the supported install -- so the module owns one
 *   prefix, records the version in it, symlinks the launcher onto PATH, and
 *   writes the .desktop entry the vendor did not ship. The version stamp is
 *   what makes a rerun free: these downloads are hundreds of megabytes.
 *
 *   A REPLACED DISTRO PACKAGE (Thunderbird) has to have the distro's version
 *   removed FIRST. A snap and a tarball both claiming `thunderbird` leave
 *   whichever the PATH finds first, which is not the one that was configured.
 *
 *   A PATCHED PACKAGE (Yandex Browser) keeps the vendor's .desktop entry and
 *   edits only its Exec line, because the rest of that entry is the vendor's
 *   to maintain.
 *
 * Hermetic: every prefix is inside the sandbox, curl serves canned metadata and
 * builds the tarball layout each vendor really ships, and nothing escalates
 * outside the sandbox.
 *
 * Replaces test/unit/datagrip_module.sh, telegram_module.sh,
 * thunderbird_module.sh, yandex_browser_module.sh, rust_module.sh and
 * mirrors_module.sh. See test/harness.h.
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
static void file_is(const char *rel, const char *expected, const char *label) {
    char *got = read_rel(rel);
    osr_assert_eq(expected, got, label);
    free(got);
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

static int build(const char *fn) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "build", "run", fn, (const char *)NULL);
}
static int run_module(const char *name) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "module", "run", name, (const char *)NULL);
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
    hs_path(&p, hs_text(&sb.osr_root), "..");
    osr_sb_env(&sb, "OSR_DOTFILES", hs_text(&p));
    hs_path(&p, hs_text(&sb.root), "scratch");
    osr_sb_mkdir(&sb, "scratch");
    osr_sb_env(&sb, "TMPDIR", hs_text(&p));
    /* Both vendor trees go inside the sandbox rather than into a real /opt. */
    osr_sb_env(&sb, "OSR_DATAGRIP_PREFIX", at("opt/datagrip"));
    osr_sb_env(&sb, "OSR_TELEGRAM_PREFIX", at("opt/telegram-desktop"));
    osr_sb_mask(&sb, "tmp.");

    osr_sb_stub_body(&sb, "dpkg", "exit 1\n");
    osr_sb_stub_body(&sb, "apt-mark", "exit 0\n");
    osr_sb_stub_body(&sb, "apt-get",
        "printf 'apt-get %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    osr_sb_stub_body(&sb, "update-desktop-database", "exit 0\n");
    /* Real tar and xz: these two builders unpack a tarball this test built,
     * and what is under test is what LANDS, not the tar argv. */
    osr_sb_real(&sb, "tar");
    osr_sb_real(&sb, "gzip");
    osr_sb_real(&sb, "xz");

    /* ================================================================
     * 1. DataGrip -- a JetBrains tarball
     * ================================================================ */
    /* curl answers the JetBrains releases feed, and where a download is asked
     * for it builds the layout the vendor really ships: DataGrip-<version>/ at
     * the root, product-info.json beside bin/. */
    osr_sb_stub_body(&sb, "curl",
        "printf 'curl %s\\n' \"$*\" >>\"$LOG\"\n"
        "_dest=; _url=; _prev=\n"
        "for _a in \"$@\"; do\n"
        "  [ \"$_prev\" = \"-o\" ] && _dest=$_a\n"
        "  case \"$_a\" in http*) _url=$_a ;; esac\n"
        "  _prev=$_a\n"
        "done\n"
        "if [ -z \"$_dest\" ]; then\n"
        "  printf '{\"DG\":[{\"version\":\"%s\",\"downloads\":{"
        "\"linux\":{\"link\":\"https://download.jetbrains.com/datagrip/datagrip-%s.tar.gz\","
        "\"size\":1082236229},"
        "\"linuxARM64\":{\"link\":\"https://download.jetbrains.com/datagrip/datagrip-%s-aarch64.tar.gz\","
        "\"size\":1105857143}}}]}\\n' \"$DGVER\" \"$DGVER\" \"$DGVER\"\n"
        "  exit 0\n"
        "fi\n"
        "_stage=$TMPROOT/fake; rm -rf \"$_stage\"\n"
        "mkdir -p \"$_stage/DataGrip-$DGVER/bin\"\n"
        "printf '{\"name\":\"DataGrip\",\"version\":\"%s\",\"buildNumber\":\"262.9437.163\"}\\n' "
        "\"$DGVER\" >\"$_stage/DataGrip-$DGVER/product-info.json\"\n"
        "printf '#!/bin/sh\\n' >\"$_stage/DataGrip-$DGVER/bin/datagrip\"\n"
        "chmod +x \"$_stage/DataGrip-$DGVER/bin/datagrip\"\n"
        ": >\"$_stage/DataGrip-$DGVER/bin/datagrip.png\"\n"
        "tar -czf \"$_dest\" -C \"$_stage\" \"DataGrip-$DGVER\"\n"
        "exit 0\n");
    osr_sb_env(&sb, "TMPROOT", hs_text(&sb.root));
    osr_sb_env(&sb, "DGVER", "2026.2.3");
    osr_sb_env(&sb, "OSR_DESKTOP_DIRS", at("usr/share/applications"));
    osr_sb_mkdir(&sb, "usr/share/applications");

    build("provide_datagrip");
    ran("datagrip-2026.2.3.tar.gz",
        "datagrip: the tarball URL comes out of the JetBrains feed, so no "
        "version is hard-coded anywhere");
    file_is("opt/datagrip/product-info.json",
        "{\"name\":\"DataGrip\",\"version\":\"2026.2.3\",\"buildNumber\":\"262.9437.163\"}\n",
        "datagrip: the tree lands at the prefix, product-info.json and all -- "
        "that file IS the version stamp, so a rerun can read it back");
    ran("ln -sf ROOT/opt/datagrip/bin/datagrip /usr/local/bin/datagrip",
        "datagrip: the launcher is symlinked onto PATH from /usr/local/bin, "
        "which precedes /usr/bin and is also the builder's own probe");
    osr_assert_absent(&sb, "opt/.datagrip-staging",
        "datagrip: the staging directory is cleaned up rather than left in /opt");

    /* SS2, and it is worth about a gigabyte: a tree already at the current
     * version skips the download -- but the .desktop entry is still repaired,
     * because a box can lose one without losing the tree. */
    osr_sb_rm(&sb, "usr/share/applications");
    osr_sb_mkdir(&sb, "usr/share/applications");
    build("provide_datagrip");
    did_not("datagrip-2026.2.3.tar.gz",
        "datagrip: a tree already at the current version skips the download "
        "entirely (SS2) -- this one is about a gigabyte");
    said("already the current release",
        "datagrip: and says why it did nothing");

    /* An upgrade is recognised by the version in the tree, not by presence. */
    osr_sb_env(&sb, "DGVER", "2026.3.1");
    build("provide_datagrip");
    said("upgrading DataGrip 2026.2.3 -> 2026.3.1",
        "datagrip: a newer release upgrades the tree, and names both versions");
    file_is("opt/datagrip/product-info.json",
        "{\"name\":\"DataGrip\",\"version\":\"2026.3.1\",\"buildNumber\":\"262.9437.163\"}\n",
        "datagrip: and the tree is the new one afterwards");

    /* ================================================================
     * 2. Telegram -- a vendor tarball with its own updater
     * ================================================================ */
    osr_sb_stub_body(&sb, "curl",
        "printf 'curl %s\\n' \"$*\" >>\"$LOG\"\n"
        "_dest=; _prev=\n"
        "for _a in \"$@\"; do [ \"$_prev\" = \"-o\" ] && _dest=$_a; _prev=$_a; done\n"
        "if [ -z \"$_dest\" ]; then\n"
        /* The vendor publishes the version as a redirect, so the probe is a
         * HEAD whose Location carries the tarball name. */
        "  printf 'HTTP/1.1 302 Found\\r\\nLocation: https://td.telegram.org/tlinux/tsetup.%s.tar.xz\\r\\n\\r\\n' \"$TGVER\"\n"
        "  printf 'HTTP/1.1 200 OK\\r\\nContent-Length: 77785992\\r\\n\\r\\n'\n"
        "  exit 0\n"
        "fi\n"
        "_stage=$TMPROOT/fake; rm -rf \"$_stage\"; mkdir -p \"$_stage/Telegram\"\n"
        "printf '#!/bin/sh\\n' >\"$_stage/Telegram/Telegram\"\n"
        "printf '#!/bin/sh\\n' >\"$_stage/Telegram/Updater\"\n"
        "chmod +x \"$_stage/Telegram/Telegram\" \"$_stage/Telegram/Updater\"\n"
        "tar -cJf \"$_dest\" -C \"$_stage\" Telegram\n"
        "exit 0\n");
    osr_sb_env(&sb, "TGVER", "7.0.9");

    build("provide_telegram");
    ran("tsetup.7.0.9.tar.xz",
        "telegram: the version comes out of the vendor's redirect, not a "
        "hard-coded number");
    file_is("opt/telegram-desktop/.osr-version", "7.0.9\n",
        "telegram: the version is stamped into the tree, because the vendor's "
        "tarball carries nothing that says which release it is");
    ran("ln -sf ROOT/opt/telegram-desktop/Telegram /usr/local/bin/telegram-desktop",
        "telegram: the binary is symlinked onto PATH");
    ran("chown -R tester ROOT/opt/telegram-desktop",
        "telegram: and the tree is handed to the USER -- Telegram ships its "
        "own updater, and a root-owned tree makes it fail silently forever");

    build("provide_telegram");
    did_not("telegram.tar.xz",
        "telegram: a tree already at that version downloads no tarball (SS2) "
        "-- the version probe is a HEAD and still runs, which is how the "
        "module knows there is nothing newer to fetch");
    said("already installed", "telegram: and it says so");

    osr_sb_env(&sb, "TGVER", "7.1.0");
    build("provide_telegram");
    file_is("opt/telegram-desktop/.osr-version", "7.1.0\n",
        "telegram: a newer release upgrades the tree and restamps it");

    /* ================================================================
     * 3. Yandex Browser -- patch the vendor's own .desktop entry
     *
     * The flags exist because the browser is a memory hog on a small machine.
     * Rewriting the whole entry would mean maintaining the vendor's MIME
     * types, translations and actions forever, so only Exec is touched.
     * ================================================================ */
    osr_sb_rm(&sb, "usr/share/applications");
    osr_sb_mkdir(&sb, "usr/share/applications");
    osr_sb_write(&sb, "usr/share/applications/yandex-browser.desktop",
        "[Desktop Entry]\n"
        "Name=Yandex Browser\n"
        "Exec=/usr/bin/yandex-browser-stable %U\n"
        "Icon=yandex-browser\n"
        "Type=Application\n"
        "\n"
        "[Desktop Action new-window]\n"
        "Name=New Window\n"
        "Exec=/usr/bin/yandex-browser-stable\n", 0644);
    /* The switch list is one file in the dotfiles, so the module has ONE copy
     * of it rather than a list smeared through the module. Pointed at a
     * sandbox copy here so the scenario states its own flags. */
    osr_sb_write(&sb, "dotfiles/yandex-browser/flags.conf",
        "# a comment explaining the next line\n"
        "--process-per-site\n"
        "--renderer-process-limit=4\n"
        "--incognito\n", 0644);
    osr_sb_env(&sb, "OSR_DOTFILES", at("dotfiles"));
    osr_sb_env(&sb, "OSR_DESKTOP_DIRS", at("usr/share/applications"));

    run_module("yandex-browser");
    holds("home/.local/share/applications/yandex-browser.desktop",
        "--process-per-site",
        "yandex: the low-RAM flags reach the Exec line");
    holds("home/.local/share/applications/yandex-browser.desktop",
        "--incognito %U",
        "yandex: and they land BEFORE the %U field code, which has to stay "
        "last or the URL is passed as a flag");
    holds("home/.local/share/applications/yandex-browser.desktop", "Name=Yandex Browser",
        "yandex: the rest of the entry is left exactly as the vendor packaged "
        "it -- the patched copy goes into the USER's applications directory, "
        "which XDG ranks above the system one, so the vendor's own file is "
        "never touched and a package update cannot fight it (SS5)");
    lacks("home/.local/share/applications/yandex-browser.desktop",
        "# a comment explaining",
        "yandex: comments in flags.conf are stripped rather than passed to the "
        "browser as switches");
    holds("home/.local/share/applications/yandex-browser.desktop",
        "[Desktop Action new-window]",
        "yandex: the action entries survive");

    osr_sb_rm(&sb, "usr/share/applications/yandex-browser.desktop");
    run_module("yandex-browser");
    said("low-RAM flags",
        "yandex: with no .desktop entry to patch it warns rather than silently "
        "doing nothing -- the flags are the whole point of the module");

    /* ================================================================
     * 3b. Thunderbird -- replace what the distro shipped, in the right order
     *
     * Ubuntu ships Thunderbird as a snap plus a transitional deb that points
     * at it. Both have to go BEFORE the real one is installed, and the order
     * is the whole point: the source: row's own probe is `command -v
     * thunderbird`, which a snap on PATH satisfies -- so a de-snap that ran
     * afterwards would leave the snap in place and install nothing.
     * ================================================================ */
    hs_path(&p, hs_text(&sb.osr_root), "..");
    osr_sb_env(&sb, "OSR_DOTFILES", hs_text(&p));
    osr_sb_env(&sb, "OSR_THEME", "xin");
    hs_path(&p, hs_text(&sb.osr_root), "themes/xin");
    osr_sb_env(&sb, "OSR_THEME_DIR", hs_text(&p));
    /* `snap list thunderbird` succeeding is how the module knows a snap is
     * installed at all. */
    osr_sb_stub_body(&sb, "snap",
        "printf 'snap %s\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    /* The archive package on 24.04+ is a stub whose only job is to install
     * that snap, and its VERSION string is what says so -- which is why the
     * purge is conditional rather than unconditional: purging a real
     * thunderbird deb on a distro that ships one would be vandalism. */
    osr_sb_stub_body(&sb, "dpkg",
        "if [ \"$1\" = \"-s\" ] && [ \"$2\" = thunderbird ]; then\n"
        "  printf 'Package: thunderbird\\nVersion: 1:2snap1-0ubuntu2\\n'\n"
        "  exit 0\n"
        "fi\n"
        "printf 'dpkg %s\\n' \"$*\" >>\"$LOG\"\nexit 1\n");
    osr_sb_stub_body(&sb, "thunderbird", "exit 0\n");
    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "home/.thunderbird/aaa.default");
    osr_sb_mkdir(&sb, "home/.thunderbird/bbb.work");
    osr_sb_write(&sb, "home/.thunderbird/profiles.ini",
        "[Profile0]\nPath=aaa.default\n\n[Profile1]\nPath=bbb.work\n", 0644);
    run_module("thunderbird");

    ran("snap remove --purge thunderbird",
        "thunderbird: the snap is removed");
    ran("dpkg --purge --force-all thunderbird",
        "thunderbird: and the transitional deb that points at it");
    {
        /* Order, stated as order: the de-snap must come before anything that
         * probes for `thunderbird` on PATH. */
        const char *log = osr_sb_log(&sb);
        const char *desnap = strstr(log, "snap remove --purge");
        const char *install = strstr(log, "apt-get install");
        osr_assert_true(desnap != NULL && (install == NULL || desnap < install),
            "thunderbird: the de-snap runs BEFORE the install -- the source: "
            "row's probe is `command -v thunderbird`, which a snap satisfies, "
            "so the other order silently installs nothing");
    }
    {
        static const char *const profiles[] = { "aaa.default", "bbb.work", NULL };
        int i;
        for (i = 0; profiles[i] != NULL; i++) {
            HStr js, css, label;
            hs_init(&js); hs_init(&css); hs_init(&label);
            hs_add(&js, "home/.thunderbird/");
            hs_add(&js, profiles[i]);
            hs_add(&js, "/user.js");
            hs_add(&css, "home/.thunderbird/");
            hs_add(&css, profiles[i]);
            hs_add(&css, "/chrome/userChrome.css");

            hs_reset(&label);
            hs_add(&label, profiles[i]);
            hs_add(&label, ": user.js enables the userChrome.css customisation "
                           "Thunderbird disables by default");
            holds(hs_text(&js),
                "toolkit.legacyUserProfileCustomizations.stylesheets\", true",
                hs_text(&label));

            hs_reset(&label);
            hs_add(&label, profiles[i]);
            hs_add(&label, ": user.js turns on the native Exchange backend");
            holds(hs_text(&js), "mail.ews.enabled\", true", hs_text(&label));

            hs_reset(&label);
            hs_add(&label, profiles[i]);
            hs_add(&label, ": the theme's chrome colours are installed -- BOTH "
                           "profiles, because a user with two would otherwise "
                           "get one themed and one not");
            holds(hs_text(&css), "--osr-accent", hs_text(&label));

            hs_free(&js); hs_free(&css); hs_free(&label);
        }
    }
    osr_sb_rm(&sb, "bin/thunderbird");
    osr_sb_rm(&sb, "bin/snap");
    osr_sb_stub_body(&sb, "dpkg", "exit 1\n");
    osr_sb_env(&sb, "OSR_THEME", "");
    osr_sb_env(&sb, "OSR_THEME_DIR", "");

    /* ================================================================
     * 4. rust -- a toolchain that installs itself, as the user
     * ================================================================ */
    hs_path(&p, hs_text(&sb.osr_root), "..");
    osr_sb_env(&sb, "OSR_DOTFILES", hs_text(&p));
    osr_sb_stub_body(&sb, "curl",
        "printf 'curl %s\\n' \"$*\" >>\"$LOG\"\n"
        "printf 'true\\n'\n");
    osr_sb_stub_body(&sb, "sh",
        "if [ \"$1\" = \"-s\" ]; then\n"
        "  printf 'sh -s %s\\n' \"$*\" >>\"$LOG\"; cat >/dev/null; exit 0\n"
        "fi\n"
        "exec /bin/sh \"$@\"\n");
    osr_sb_stub_body(&sb, "bash",
        "printf 'bash %s\\n' \"$*\" >>\"$LOG\"\ncat >/dev/null\nexit 0\n");
    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "home");
    run_module("rust");
    ran("sudo -u tester sh -s -- -y --default-toolchain stable",
        "rust: rustup is piped into a shell AS THE USER -- a toolchain "
        "installed as root lands in root's home and the user never sees it");
    ran("sudo -u tester bash",
        "rust: and cargo-binstall's own installer likewise");

    /* SS2: an existing toolchain is not reinstalled. */
    osr_sb_write(&sb, "home/.cargo/bin/cargo", "#!/bin/sh\n", 0755);
    run_module("rust");
    did_not("sh -s -- -y",
        "rust: an existing cargo means rustup is not run again (SS2)");
    said("skipping", "rust: and it says so");

    /* ================================================================
     * 5. mirrors -- the multi-minute probe that must run once
     * ================================================================ */
    osr_sb_env(&sb, "OSR_PKG", "pacman");
    osr_sb_env(&sb, "OSR_DISTRO", "arch");
    osr_sb_env(&sb, "OSR_ID_LIKE", "");
    osr_sb_env(&sb, "OSR_CODENAME", "");
    osr_sb_env(&sb, "OSR_VERSION_ID", "");
    osr_sb_env(&sb, "OSR_MIRRORS_N", "2");
    osr_sb_env(&sb, "OSR_PACMAN_DIR", at("etc/pacman.d"));
    osr_sb_write(&sb, "etc/pacman.d/mirrorlist",
        "Server = https://one.example/$repo/os/$arch\n"
        "Server = https://two.example/$repo/os/$arch\n"
        "Server = https://three.example/$repo/os/$arch\n", 0644);
    osr_sb_stub_body(&sb, "pacman",
        "case \"$1\" in -Q*) exit 1 ;; esac\n"
        "printf 'pacman %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    /* rankmirrors is the multi-minute part; it echoes back what it was fed. */
    osr_sb_stub_body(&sb, "rankmirrors",
        "printf 'rankmirrors %s\\n' \"$*\" >>\"$LOG\"\n"
        "for _a in \"$@\"; do _last=$_a; done\n"
        "printf '# ranked from: %s\\n' \"$_last\"\n"
        "grep '^Server' \"$_last\" 2>/dev/null | head -n \"${OSR_MIRRORS_N:-16}\"\n");

    run_module("mirrors");
    ran("pacman-contrib",
        "mirrors: the ranker itself is installed first -- rankmirrors is not "
        "in the base system");
    holds("etc/pacman.d/mirrorlist.backup", "three.example",
        "mirrors: the pristine list is backed up before it is replaced, with "
        "every mirror still in it");
    lacks("etc/pacman.d/mirrorlist", "three.example",
        "mirrors: and the live list is trimmed to the requested count");
    ran("pacman -Sy",
        "mirrors: the index is refreshed after the swap, because every mirror "
        "the box knew about has just changed");

    run_module("mirrors");
    said("already ranked",
        "mirrors: a second run skips the probe (SS2) -- it takes minutes, and "
        "a rice install that redid it every time would be unusable");
    did_not("rankmirrors", "mirrors: and nothing is measured again");

    osr_sb_env(&sb, "OSR_MIRRORS_FORCE", "1");
    run_module("mirrors");
    ran("rankmirrors",
        "mirrors: OSR_MIRRORS_FORCE=1 re-ranks -- mirror quality ages, so "
        "there has to be a way to redo it without deleting a stamp file");
    osr_sb_env(&sb, "OSR_MIRRORS_FORCE", "");

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
