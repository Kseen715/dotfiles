/* test/unit_c/build_test.c -- the `source:` builders: everything os-rice
 * installs that no package manager on the box carries.
 *
 * A builder is the least safe thing in the tree. It reaches the network, it
 * unpacks an archive, and it runs `install` and `make install` as root -- so
 * the ASSET NAME is the whole game. Every project spells its release assets
 * differently and changes that spelling between versions:
 *
 *   btop     btop-x86_64-unknown-linux-musl.tar.gz   (uname arch)
 *   gh       gh_1.2.3_linux_amd64.tar.gz             (dpkg arch, no `v`)
 *   lsd      lsd-v1.2.3-x86_64-...tar.gz             (tag repeated, `v` and all)
 *   fzf      fzf-1.2.3-linux_amd64.tar.gz            (no `v`, underscore arch)
 *
 * Get one wrong and the download 404s, which `curl -f` turns into a failure --
 * so the failure is at least loud. Get the ARCH wrong and it silently installs
 * the binary for another machine. Both are asserted, per builder, by name.
 *
 * IDEMPOTENCY IS SOMETIMES A VERSION, NOT PRESENCE. fzf and chafa are already
 * installed on most boxes at a version too old to be useful -- fzf below 0.66
 * has no --gutter and its up-arrow widget dies the moment it is pressed, and
 * chafa below 1.16 has no --probe and yazi's image preview does nothing. So
 * those two builders compare versions rather than asking `command -v`.
 *
 * Hermetic: curl serves canned release JSON and writes a payload where the
 * fetch asked for one; tar and unzip PLANT the binaries the builder is about
 * to look for, inside the sandbox only -- a builder that unpacks into a real
 * prefix (zig into /usr/local/zig-<v>) must not have that prefix created here.
 * Everything that mutates the system logs and does nothing.
 *
 * Replaces test/unit/build_c_parity.sh, chafa_provider.sh, fzf_provider.sh,
 * yazi_provider.sh, ueberzug_module.sh and amneziavpn_source.sh. See
 * test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

/* plant -- which binaries the archive stubs lay down when they "unpack". A
 * builder then finds them with its own search and installs them. */
static void plant(const char *names) {
    osr_sb_env(&sb, "PLANT", names);
}

/* plant_paths -- for the builders that unpack a SOURCE tree and run something
 * out of it: each entry is a path relative to the -C dir, optionally
 * `path=content`. Without content it is written executable, because that is
 * what ./configure and ./get-deps have to be. */
static void plant_paths(const char *paths) {
    osr_sb_env(&sb, "PLANTPATHS", paths);
}

/* build -- `osr build run <fn>`, from a clean scratch directory. */
static int build(const char *fn) {
    osr_sb_rm(&sb, "scratch");
    osr_sb_mkdir(&sb, "scratch");
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "build", "run", fn, (const char *)NULL);
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
    /* Pinned so the `-j` a builder passes is the scenario's rather than the
     * build machine's core count. */
    osr_sb_env(&sb, "OSR_BUILD_JOBS", "4");
    hs_path(&p, hs_text(&sb.root), "scratch");
    osr_sb_env(&sb, "TMPDIR", hs_text(&p));
    osr_sb_env(&sb, "TMPROOT", hs_text(&sb.root));
    /* The archive stubs need a real mkdir to plant with, and their own name is
     * a logging stub by then. */
    osr_sb_env(&sb, "REALMKDIR", "/bin/mkdir");
    plant("");
    plant_paths("");

    /* Every mktemp path carries a random suffix; it is not a decision a
     * builder made about the box. */
    osr_sb_mask(&sb, "tmp.");

    /* --- curl: canned release metadata, and a payload where asked ------- */
    osr_sb_stub_body(&sb, "curl",
        "printf 'curl %s\\n' \"$*\" >>\"$LOG\"\n"
        "_dest=; _url=; _prev=\n"
        "for _a in \"$@\"; do\n"
        "  [ \"$_prev\" = \"-o\" ] && _dest=$_a\n"
        "  case \"$_a\" in https://*) _url=$_a ;; esac\n"
        "  _prev=$_a\n"
        "done\n"
        "case \"$_url\" in\n"
        "  *api.github.com*/releases/latest) _json='{\"tag_name\": \"v1.2.3\"}' ;;\n"
        "  *api.github.com*) _json='[{\"name\": \"v1.2.3\"}]' ;;\n"
        /* Both asset namings upstream has used -- zig-<arch>-linux on 0.15+,
         * zig-linux-<arch> on 0.14 and older -- newest first, which is the
         * order the "no version pinned" path relies on. */
        "  *ziglang.org*index.json) _json='{\n"
        "  \"0.15.1\": {\n"
        "    \"x86_64-linux\": { \"tarball\": \"https://ziglang.org/download/0.15.1/zig-x86_64-linux-0.15.1.tar.xz\" },\n"
        "    \"aarch64-linux\": { \"tarball\": \"https://ziglang.org/download/0.15.1/zig-aarch64-linux-0.15.1.tar.xz\" }\n"
        "  },\n"
        "  \"0.14.1\": {\n"
        "    \"x86_64-linux\": { \"tarball\": \"https://ziglang.org/download/0.14.1/zig-linux-x86_64-0.14.1.tar.xz\" }\n"
        "  }\n"
        "}' ;;\n"
        "  *) _json= ;;\n"
        "esac\n"
        "if [ -n \"$_dest\" ]; then printf 'payload\\n' >\"$_dest\"\n"
        "elif [ -n \"$_json\" ]; then printf '%s\\n' \"$_json\"\n"
        "fi\n"
        "exit 0\n");

    /* --- tar / unzip: log, then plant what the builder will look for ---- */
    osr_sb_stub_body(&sb, "tar",
        "printf 'tar %s\\n' \"$*\" >>\"$LOG\"\n"
        "_dir=; _prev=\n"
        "for _a in \"$@\"; do [ \"$_prev\" = \"-C\" ] && _dir=$_a; _prev=$_a; done\n"
        "[ -n \"$_dir\" ] || exit 0\n"
        /* Only ever inside the sandbox: a builder unpacking into a real
         * prefix must not have that prefix created by a test. */
        "case \"$_dir\" in \"$TMPROOT\"*) ;; *) exit 0 ;; esac\n"
        "\"$REALMKDIR\" -p \"$_dir/inner\"\n"
        "for _b in $PLANT; do\n"
        "  printf '#!/bin/sh\\n' >\"$_dir/inner/$_b\"; chmod +x \"$_dir/inner/$_b\"\n"
        "done\n"
        "for _p in $PLANTPATHS; do\n"
        "  case \"$_p\" in *=*) _rel=${_p%%=*}; _txt=${_p#*=} ;; *) _rel=$_p; _txt= ;; esac\n"
        /* Only make a parent when there IS one: `${x%/*}` on a slash-less
         * path yields the path itself, which would create a DIRECTORY where
         * the file is supposed to go. */
        "  case \"$_rel\" in */*) \"$REALMKDIR\" -p \"$_dir/${_rel%/*}\" ;; esac\n"
        "  if [ -n \"$_txt\" ]; then printf '%s\\n' \"$_txt\" >\"$_dir/$_rel\"\n"
        "  else\n"
        "    printf '#!/bin/sh\\nprintf \"%%s %%s%%s\\\\n\" \"$0\" \"$*\" "
        "\"${PKG_CONFIG_PATH:+ PKG_CONFIG_PATH=$PKG_CONFIG_PATH}\" >>\"$LOG\"\\n' "
        ">\"$_dir/$_rel\"\n"
        "    chmod +x \"$_dir/$_rel\"\n"
        "  fi\n"
        "done\n"
        "exit 0\n");
    osr_sb_stub_body(&sb, "unzip",
        "printf 'unzip %s\\n' \"$*\" >>\"$LOG\"\n"
        "_dir=; _prev=\n"
        "for _a in \"$@\"; do [ \"$_prev\" = \"-d\" ] && _dir=$_a; _prev=$_a; done\n"
        "[ -n \"$_dir\" ] || exit 0\n"
        "case \"$_dir\" in \"$TMPROOT\"*) ;; *) exit 0 ;; esac\n"
        "\"$REALMKDIR\" -p \"$_dir/inner\"\n"
        "for _b in $PLANT; do\n"
        "  printf '#!/bin/sh\\n' >\"$_dir/inner/$_b\"; chmod +x \"$_dir/inner/$_b\"\n"
        "done\n"
        "exit 0\n");

    osr_sb_stub_body(&sb, "git",
        "printf 'git %s\\n' \"$*\" >>\"$LOG\"\n"
        "[ \"$1\" = clone ] || exit 0\n"
        "for _a in \"$@\"; do _dst=$_a; done\n"
        "case \"$_dst\" in \"$TMPROOT\"*) ;; *) exit 0 ;; esac\n"
        "\"$REALMKDIR\" -p \"$_dst/assets/icon\" \"$_dst/target/release\"\n"
        /* ./get-deps is upstream's own dependency installer, run out of the
         * checkout, so it has to be a real executable to be reachable. */
        "printf '#!/bin/sh\\nprintf \"get-deps %%s\\\\n\" \"$*\" >>\"$LOG\"\\n' "
        ">\"$_dst/get-deps\"\n"
        "chmod +x \"$_dst/get-deps\"\n"
        ": >\"$_dst/assets/wezterm.desktop\"\n"
        ": >\"$_dst/assets/icon/terminal.png\"\n"
        ": >\"$_dst/PKGBUILD\"\n"
        "exit 0\n");

    /* Everything that mutates the machine: logs, does nothing. */
    {
        static const char *const quiet[] = {
            "install", "apt-get", "make", "makepkg", "pacman", "ldconfig",
            "zig", "mkdir", "ln", "dnf", "bash", "apt-mark", NULL
        };
        int i;
        for (i = 0; quiet[i] != NULL; i++) {
            HStr body;
            hs_init(&body);
            hs_add(&body, "printf '");
            hs_add(&body, quiet[i]);
            hs_add(&body, " %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
            osr_sb_stub_body(&sb, quiet[i], hs_text(&body));
            hs_free(&body);
        }
    }
    osr_sb_stub_body(&sb, "dpkg", "exit 1\n");
    osr_sb_stub_body(&sb, "rpm", "exit 1\n");
    /* cmake reports the parallelism it was given through the environment, so
     * the -j a builder chose is visible in the log. */
    osr_sb_stub_body(&sb, "cmake",
        "printf 'cmake %s%s\\n' \"$*\" "
        "\"${CMAKE_BUILD_PARALLEL_LEVEL:+ jobs=$CMAKE_BUILD_PARALLEL_LEVEL}\" >>\"$LOG\"\n"
        "exit 0\n");

    /* ================================================================
     * 1. The tarball builders -- one asset naming each
     * ================================================================ */
    plant("gh");
    build("provide_gh_tarball");
    ran("gh_1.2.3_linux_amd64.tar.gz",
        "gh: the asset carries the resolved version with no `v`, and the DPKG "
        "arch");
    ran("install -m 0755",
        "gh: the binary is installed 0755 -- an executable nobody can execute "
        "is the same as one that is not there");
    ran("/usr/local/bin/gh",
        "gh: into /usr/local/bin, which is ours and not the package manager's");
    ran("sudo install",
        "gh: and it escalates to put it there");

    plant("btop");
    build("provide_btop_tarball");
    ran("btop-x86_64-unknown-linux-musl.tar.gz",
        "btop: the asset arch is uname-style, not dpkg-style");

    plant("lsd");
    build("provide_lsd_tarball");
    ran("lsd-v1.2.3-x86_64-unknown-linux-gnu.tar.gz",
        "lsd: the asset repeats the tag, `v` and all");

    plant("fastfetch");
    build("provide_fastfetch_tarball");
    ran("fastfetch-linux-amd64.tar.gz",
        "fastfetch: x86_64 asks for the amd64 asset");

    /* ================================================================
     * 2. Arch handling
     * ================================================================ */
    osr_sb_env(&sb, "OSR_ARCH", "aarch64");
    plant("btop");
    build("provide_btop_tarball");
    ran("btop-aarch64-unknown-linux-musl.tar.gz",
        "btop: aarch64 resolves to its own asset");

    osr_sb_env(&sb, "OSR_ARCH", "riscv64");
    plant("btop");
    build("provide_btop_tarball");
    did_not("install -m 0755",
        "btop: an architecture upstream ships no asset for installs NOTHING -- "
        "the alternative is putting an x86_64 binary on a riscv box");
    osr_sb_env(&sb, "OSR_ARCH", "x86_64");

    /* ================================================================
     * 3. fzf -- idempotency by VERSION
     * ================================================================ */
    plant("fzf");
    build("provide_fzf");
    ran("fzf-1.2.3-linux_amd64.tar.gz",
        "fzf: with no fzf on the box the release binary is fetched");

    osr_sb_stub_body(&sb, "fzf", "printf '0.30.0 (abc)\\n'\n");
    plant("fzf");
    build("provide_fzf");
    ran("fzf-1.2.3-linux_amd64.tar.gz",
        "fzf: an fzf OLDER than the minimum is replaced even though it is "
        "present -- below 0.66 there is no --gutter, and the up-arrow history "
        "widget dies the moment it is pressed");

    osr_sb_stub_body(&sb, "fzf", "printf '0.74.3 (abc)\\n'\n");
    build("provide_fzf");
    did_not("curl", "fzf: a new enough fzf is left alone (SS2)");
    said("already", "fzf: and the version comparison is reported");
    osr_sb_rm(&sb, "bin/fzf");

    /* ================================================================
     * 4. The .deb builders
     * ================================================================ */
    plant("lsd");
    build("provide_lsd_deb");
    ran("lsd_1.2.3_amd64.deb", "lsd: the .deb carries the dpkg arch");
    ran("apt-get install -y",
        "lsd: and apt installs it, so the package database knows about it -- "
        "dpkg -i would leave dependencies unresolved");
    ran("DEBIAN_FRONTEND=noninteractive",
        "lsd: noninteractively, because nothing is watching");

    plant("fastfetch");
    build("provide_fastfetch_deb");
    ran("fastfetch-linux-amd64.deb", "fastfetch: the .deb asset naming");

    osr_sb_env(&sb, "OSR_ARCH", "riscv64");
    osr_sb_env(&sb, "OSR_ARCH_DEB", "riscv64");
    plant("fastfetch");
    build("provide_fastfetch_deb");
    ran("fastfetch-linux-riscv64.deb",
        "fastfetch: an arch with no special-case falls back to the dpkg name "
        "rather than giving up -- upstream may well ship it");
    osr_sb_env(&sb, "OSR_ARCH", "x86_64");
    osr_sb_env(&sb, "OSR_ARCH_DEB", "amd64");

    /* ================================================================
     * 5. yazi -- a .zip holding TWO binaries, with a cargo fallback
     * ================================================================ */
    plant("yazi ya");
    build("provide_yazi_bin");
    ran("yazi-x86_64-unknown-linux-gnu.zip", "yazi: the release .zip");
    ran("/usr/local/bin/yazi", "yazi: the main binary is installed");
    ran("/usr/local/bin/ya",
        "yazi: and `ya` alongside it -- the plugin manager ships in the same "
        "archive and yazi's plugin commands are useless without it");

    osr_sb_env(&sb, "OSR_ARCH", "riscv64");
    plant("yazi ya");
    build("provide_yazi_bin");
    did_not("unzip",
        "yazi: an arch with no prebuilt asset does not download one");
    said("cargo",
        "yazi: it falls back to building from source instead of failing -- "
        "yazi is a Rust program and cargo can produce it anywhere");
    osr_sb_env(&sb, "OSR_ARCH", "x86_64");

    /* ================================================================
     * 6. zig -- a whole TREE, resolved out of an index
     * ================================================================ */
    plant("zig");
    build("provide_zig");
    ran("zig-x86_64-linux-0.15.1.tar.xz",
        "zig: with nothing pinned the newest release in the index is taken");
    ran("-C /usr/local/zig-0.15.1 --strip-components=1",
        "zig: it is a whole tree, so it unpacks into a versioned prefix");
    ran("ln -sf /usr/local/zig-0.15.1/zig /usr/local/bin/zig",
        "zig: and one symlink puts it on PATH -- which is also what makes an "
        "upgrade atomic and a rollback possible");

    osr_sb_env(&sb, "ZIG_VERSION", "0.14.1");
    plant("zig");
    build("provide_zig");
    ran("zig-linux-x86_64-0.14.1.tar.xz",
        "zig: a pinned version picks that release -- and note the asset naming "
        "is the OTHER one, which is why the index is read rather than the URL "
        "being composed");
    ran("mkdir -p /usr/local/zig-0.14.1",
        "zig: into its own prefix, beside any other version");
    osr_sb_env(&sb, "ZIG_VERSION", "");

    osr_sb_env(&sb, "OSR_ARCH", "riscv64");
    plant("zig");
    build("provide_zig");
    said("no zig tarball for arch riscv64",
        "zig: an arch the index has no tarball for stops with a clear reason");
    did_not("tar -xf", "zig: and nothing is unpacked");
    osr_sb_env(&sb, "OSR_ARCH", "x86_64");

    /* ================================================================
     * 7. The prebuilt community routes
     * ================================================================ */
    osr_sb_env(&sb, "OSR_PKG", "dnf");
    build("provide_ghostty_copr");
    ran("dnf copr enable -y scottames/ghostty",
        "ghostty on Fedora: the community COPR is enabled");
    ran("dnf install -y ghostty",
        "ghostty on Fedora: and then it is an ordinary package");
    osr_sb_env(&sb, "OSR_PKG", "apt");

    build("provide_ghostty_deb");
    ran("bash",
        "ghostty on Debian: upstream's own installer script is piped into a "
        "shell -- there is no .deb to fetch");

    /* ================================================================
     * 8. The builders that COMPILE
     * ================================================================ */
    plant_paths("chafa-1.2.3/configure");
    build("provide_chafa");
    ran("chafa-1.2.3.tar.xz", "chafa: a source tarball, not a binary release");
    ran("configure --prefix=/usr/local",
        "chafa: configured into /usr/local, which is ours");
    ran("PKG_CONFIG_PATH=",
        "chafa: with a pkg-config path, so it finds the libraries it needs");
    ran("make -j4",
        "chafa: built with the parallelism the caller asked for");
    ran("sudo make", "chafa: and installed as root");

    osr_sb_stub_body(&sb, "chafa", "printf 'chafa version 1.20.0\\n'\n");
    plant_paths("chafa-1.2.3/configure");
    build("provide_chafa");
    did_not("configure",
        "chafa: a new enough chafa is left alone -- like fzf, this one's "
        "idempotency is a version, because below 1.16 there is no --probe and "
        "yazi's image preview silently does nothing");
    said("already", "chafa: and the comparison is reported");
    osr_sb_rm(&sb, "bin/chafa");
    plant_paths("");

    plant_paths("ueberzugpp-1.2.3/CMakeLists.txt=cmake_minimum_required(VERSION_3.10)");
    build("provide_ueberzugpp");
    ran("-DENABLE_OPENCV=OFF",
        "ueberzugpp: OpenCV is off -- it is an optional dependency that "
        "roughly triples the build and buys a terminal image viewer nothing");
    ran("jobs=4",
        "ueberzugpp: cmake's parallelism comes through the environment");
    ran("sudo cmake --install", "ueberzugpp: and it installs as root");

    plant_paths("");
    build("provide_ueberzugpp");
    did_not("cmake -S",
        "ueberzugpp: a tarball with no CMakeLists.txt is upstream having "
        "changed its layout, and configuring an empty tree would produce a "
        "confusing failure much later");

    osr_sb_env(&sb, "OSR_PKG", "pacman");
    build("provide_paru");
    ran("git clone --depth 1 https://aur.archlinux.org/paru.git",
        "paru: the chicken-and-egg AUR helper is cloned from the AUR directly");
    ran("sudo -u tester makepkg",
        "paru: and built AS THE USER -- makepkg refuses to run as root, so a "
        "build that escalated would simply stop");
    osr_sb_env(&sb, "OSR_PKG", "apt");

    /* ghostty from source bootstraps its own toolchain: it reads the exact
     * zig version its tree pins and installs THAT one before building. */
    plant_paths("ghostty-1.2.3/.zig-version=0.14.1");
    build("provide_ghostty");
    ran("zig-linux-x86_64-0.14.1.tar.xz",
        "ghostty: the zig version its own tree pins is what gets installed, "
        "not the newest -- ghostty does not build on a newer zig");
    ran("zig build -p /usr -Doptimize=ReleaseFast",
        "ghostty: and then it builds itself with it");
    plant_paths("");

    /* wezterm needs a Rust toolchain it will not install for itself: that is
     * a manifest-order decision, and saying so is more useful than a cargo
     * error 40 lines into a build. */
    build("provide_wezterm");
    said("install 'rust' before wezterm",
        "wezterm: with no cargo the builder stops and names the prerequisite "
        "rather than failing somewhere inside a build");
    did_not("git clone", "wezterm: and nothing is cloned first");

    /* ================================================================
     * 9. The version floors, one release either side
     *
     * fzf and chafa are the two builders whose idempotency is a version, and
     * a floor that is off by one release is the worst kind of wrong: the box
     * looks correct, and the feature that needed the newer version fails
     * silently at the moment somebody uses it.
     * ================================================================ */
    {
        static const struct { const char *ver; int build_it; const char *why; } fzf_cases[] = {
            { "0.29.0 (abc)",  1, "0.29 (Ubuntu 22.04) is replaced" },
            { "0.60 (devel)",  1, "0.60 (Debian 13 / Ubuntu 25.04) is replaced" },
            { "0.65.2 (abc)",  1, "0.65.2 -- ONE release under the floor -- is replaced" },
            { "0.66.0 (abc)",  0, "0.66.0, the exact minimum, is accepted" },
            { "0.74.3 (abc)",  0, "0.74.3 (Arch/Void/Alpine) is accepted" },
            { "1.0.0 (abc)",   0, "1.0.0 is accepted: a major bump beats the minor floor" }
        };
        size_t i;
        for (i = 0; i < sizeof(fzf_cases) / sizeof(fzf_cases[0]); i++) {
            HStr body, label;
            hs_init(&body);
            hs_add(&body, "printf '");
            hs_add(&body, fzf_cases[i].ver);
            hs_add(&body, "\\n'\n");
            osr_sb_stub_body(&sb, "fzf", hs_text(&body));
            hs_free(&body);
            plant("fzf");
            build("provide_fzf");
            hs_init(&label);
            hs_add(&label, "fzf: ");
            hs_add(&label, fzf_cases[i].why);
            if (fzf_cases[i].build_it) ran("curl", hs_text(&label));
            else                       did_not("curl", hs_text(&label));
            hs_free(&label);
        }
        osr_sb_rm(&sb, "bin/fzf");
    }

    {
        static const struct { const char *ver; int build_it; const char *why; } chafa_cases[] = {
            { "Chafa version 1.8.0",  1, "1.8.0 (Ubuntu 22.04) is replaced" },
            { "Chafa version 1.14.5", 1, "1.14.5 (Debian 13 / Ubuntu 24.04) is replaced: it has no --probe" },
            { "Chafa version 1.16.0", 0, "1.16.0, the exact minimum, is accepted" },
            { "Chafa version 1.18.2", 0, "1.18.2 (Arch/Void) is accepted" },
            { "Chafa version 2.0.0",  0, "2.0.0 is accepted: a major bump beats the minor floor" }
        };
        size_t i;
        for (i = 0; i < sizeof(chafa_cases) / sizeof(chafa_cases[0]); i++) {
            HStr body, label;
            hs_init(&body);
            hs_add(&body, "printf '");
            hs_add(&body, chafa_cases[i].ver);
            hs_add(&body, "\\n'\n");
            osr_sb_stub_body(&sb, "chafa", hs_text(&body));
            hs_free(&body);
            plant_paths("chafa-1.2.3/configure");
            build("provide_chafa");
            hs_init(&label);
            hs_add(&label, "chafa: ");
            hs_add(&label, chafa_cases[i].why);
            if (chafa_cases[i].build_it) ran("configure", hs_text(&label));
            else                         did_not("configure", hs_text(&label));
            hs_free(&label);
        }
        osr_sb_rm(&sb, "bin/chafa");
        plant_paths("");
    }

    /* ================================================================
     * 10. amneziavpn -- a prebuilt route with a source fallback
     *
     * Upstream ships a QtIFW installer for some releases and not others, so
     * the builder tries the binary and falls back to compiling. Which route
     * it took is not cosmetic: the source build pulls conan and takes half an
     * hour, and taking it when a binary existed is a bug nobody reports.
     * ================================================================ */
    /* The QtIFW installer upstream ships inside the release tarball. It is
     * found by suffix, run, and then thrown away. */
    plant("");
    plant_paths("AmneziaVPN.bin");
    build("provide_amneziavpn");
    ran("install --root /opt/AmneziaVPN",
        "amneziavpn: the QtIFW installer is run headless into its own prefix");
    ran("ln -sf /opt/AmneziaVPN/AmneziaVPN /usr/local/bin/amneziavpn",
        "amneziavpn: and symlinked onto PATH");
    did_not("cmake",
        "amneziavpn: nothing is compiled while a ready binary exists");
    did_not("git clone",
        "amneziavpn: and nothing is checked out either");

    osr_sb_env(&sb, "OSR_ARCH", "riscv64");
    build("provide_amneziavpn");
    said("no AmneziaVPN release binary for arch riscv64",
        "amneziavpn: an arch with no prebuilt installer says so");
    did_not("install --root",
        "amneziavpn: an arch with no prebuilt installer does not download one");
    ran("git clone",
        "amneziavpn: it falls back to the source build rather than stopping");
    osr_sb_env(&sb, "OSR_ARCH", "x86_64");
    plant_paths("");

    /* ================================================================
     * 11. Which session gets ueberzugpp at all
     *
     * modules/yazi.c installs the image adapter that yazi will actually use,
     * and yazi picks one from the session it finds itself in. Building
     * ueberzugpp on a box where yazi will use chafa is half an hour of cmake
     * for a binary nothing runs; NOT building it where yazi wants it leaves
     * image preview silently dead.
     *
     * The order matters as much as the answers: yazi checks X11 first, so a
     * box with both an X session and a stray Wayland variable is X11.
     * ================================================================ */
    {
        static const struct {
            const char *type; const char *display; const char *wayland;
            const char *compositor; int wants; const char *why;
        } sessions[] = {
            { "x11", ":0", "", "",
              1, "an X11 session installs ueberzugpp" },
            { "", ":0", "", "",
              1, "a bare DISPLAY with no XDG_SESSION_TYPE is still X11" },
            { "wayland", "", "wayland-0", "HYPRLAND_INSTANCE_SIGNATURE",
              1, "Wayland under Hyprland: yazi routes to Ueberzug, so build it" },
            { "wayland", "", "wayland-0", "SWAYSOCK",
              1, "Wayland under sway: the same" },
            { "wayland", "", "wayland-0", "",
              0, "Wayland on a compositor yazi has no Ueberzug route for: it "
                 "uses chafa, so nothing is built" },
            { "", "", "", "",
              0, "headless -- a container or an ssh session -- uses chafa" },
            { "x11", ":0", "", "SWAYSOCK",
              1, "X11 wins over a stray Wayland variable, because that is the "
                 "order yazi itself checks in" }
        };
        size_t i;
        static const char *const compositor_vars[] = {
            "NIRI_SOCKET", "SWAYSOCK", "HYPRLAND_INSTANCE_SIGNATURE",
            "WAYFIRE_SOCKET", NULL
        };

        /* yazi and a new-enough chafa are already installed, so the module
         * reaches the adapter decision instead of stopping in a build. */
        osr_sb_stub_body(&sb, "yazi", "exit 0\n");
        osr_sb_stub_body(&sb, "ya", "exit 0\n");
        osr_sb_stub_body(&sb, "chafa", "printf 'Chafa version 1.20.0\\n'\n");
        for (i = 0; i < sizeof(sessions) / sizeof(sessions[0]); i++) {
            HStr label;
            int v;
            osr_sb_env(&sb, "XDG_SESSION_TYPE", sessions[i].type);
            osr_sb_env(&sb, "DISPLAY", sessions[i].display);
            osr_sb_env(&sb, "WAYLAND_DISPLAY", sessions[i].wayland);
            for (v = 0; compositor_vars[v] != NULL; v++) {
                osr_sb_env(&sb, compositor_vars[v],
                           strcmp(compositor_vars[v], sessions[i].compositor) == 0
                               ? "/run/user/1000/sock" : "");
            }
            osr_sb_reset(&sb);
            osr_sb_run_core(&sb, "module", "run", "yazi", (const char *)NULL);
            hs_init(&label);
            hs_add(&label, "yazi image adapter: ");
            hs_add(&label, sessions[i].why);
            if (sessions[i].wants) ran("ueberzugpp", hs_text(&label));
            else                   did_not("ueberzugpp", hs_text(&label));
            hs_free(&label);
        }

        /* Whatever the session, an adapter is always installed: chafa works
         * everywhere, including over ssh, and yazi with no adapter at all
         * shows nothing where a preview should be. */
        /* Whatever the session, chafa is the adapter that is always available
         * -- and here it is already new enough, so what the log shows is that
         * the module considered it rather than that it built one. */
        osr_assert_true(strstr(osr_sb_capture_both(&sb), "chafa") != NULL,
            "yazi image adapter: chafa is accounted for even headless -- it is "
            "the adapter that works everywhere, including over ssh");
    }

    /* ================================================================
     * 12. GPaste -- a version pinned to the RUNNING GNOME Shell
     *
     * A GNOME Shell extension is compiled against one Shell major and does
     * not load on another. So the tag is picked from what gnome-shell reports,
     * not from what is newest -- a Shell 45 box handed the v50 branch gets an
     * extension that never appears, with nothing anywhere saying why.
     * ================================================================ */
    {
        /* Upstream's tags, newest first with the majors interleaved, which is
         * the shape the API really returns. */
        osr_sb_stub_body(&sb, "curl",
            "printf 'curl %s\\n' \"$*\" >>\"$LOG\"\n"
            "_dest=; _prev=\n"
            "for _a in \"$@\"; do [ \"$_prev\" = \"-o\" ] && _dest=$_a; _prev=$_a; done\n"
            "if [ -z \"$_dest\" ]; then\n"
            "  printf '[{\"name\":\"v50.7\"},{\"name\":\"v50.6\"},{\"name\":\"v50.10\"},"
            "{\"name\":\"v45.11\"},{\"name\":\"v45.3\"},{\"name\":\"v45\"}]\\n'\n"
            "  exit 0\n"
            "fi\n"
            "printf 'payload\\n' >\"$_dest\"\n"
            "exit 0\n");
        osr_sb_stub_body(&sb, "meson",
            "printf 'meson %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
        osr_sb_stub_body(&sb, "ninja",
            "printf 'ninja %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
        osr_sb_stub_body(&sb, "gpaste-client", "exit 1\n");

        osr_sb_stub_body(&sb, "gnome-shell", "printf 'GNOME Shell 50.1\\n'\n");
        plant_paths("");
        build("provide_gpaste");
        ran("v50.10",
            "gpaste: a Shell 50 box gets the NEWEST v50 tag -- v50.10, not "
            "v50.7, which is what a lexical sort would have picked");
        did_not("v45.11", "gpaste: and never a tag from another major");

        osr_sb_stub_body(&sb, "gnome-shell", "printf 'GNOME Shell 45.9\\n'\n");
        build("provide_gpaste");
        ran("v45.11",
            "gpaste: a Shell 45 box gets the newest v45 tag");
        did_not("v50.10",
            "gpaste: and NOT the newer v50 branch, which would not load at all");

        /* A Shell newer than any tag upstream has cut yet: the latest is the
         * best guess available, and better than refusing to install. */
        osr_sb_stub_body(&sb, "gnome-shell", "printf 'GNOME Shell 51.0\\n'\n");
        build("provide_gpaste");
        ran("v50.7",
            "gpaste: a Shell newer than every tag falls back to the LATEST "
            "RELEASE -- the first entry upstream lists, not the highest "
            "version-sorted tag, because at that point there is no major to "
            "match and what upstream calls current is the better guess");

        /* No gnome-shell at all: there is no major to compile against, and
         * guessing one produces an extension nothing can load. */
        osr_sb_rm(&sb, "bin/gnome-shell");
        osr_assert_true(build("provide_gpaste") != 0,
            "gpaste: with no gnome-shell the builder stops rather than guessing "
            "a major -- an extension built for the wrong Shell is silently dead");
    }

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
