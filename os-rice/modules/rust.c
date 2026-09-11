/* modules/rust.c -- Rust toolchain via rustup. ONE copy, POSIX, distro-agnostic
 * (was linux-rhel/modules/rust.sh, bash + hand-rolled `sudo -u`). The compiler +
 * curl come from pkg_install (`build` maps per distro through pkgmap); rustup
 * itself installs into OSR_USER's ~/.cargo as a user-space toolchain (§8). All
 * user work runs through as_user — never a hand-rolled `sudo -u` + chown (the
 * source of the arch drift bug, §7).
 *
 * This is a prerequisite module: list it in a rice BEFORE any cargo: row (manifest
 * order is the dependency graph, §4). ~/.cargo/bin is added to PATH by the
 * dotfiles 00-env.zsh layer (guard-style, §5) — this module does not touch PATH.
 * osr_install_rustup — stream sh.rustup.rs to sh as OSR_USER, unattended.
 * --no-modify-path: PATH is owned by the 00-env layer, not rustup's rc edits (§5).
 * osr_install_cargo_tooling — cargo-binstall + cargo-update, the pair that turns
 * every later cargo: row into a prebuilt-binary download instead of a source
 * build. binstall comes from its own release script (installing it FROM source
 * would defeat the point), and cargo-update then rides binstall. Best-effort
 * throughout: on failure plain `cargo install` still works, so warn, never error.
 * Idempotency probe (§2): rustup drops cargo at ~/.cargo/bin. If it is already
 * there, converge silently — no network round-trip, so a second run all-skips.
 * Runs on every pass: internally idempotent, and it is what makes the cargo:
 * provider (lib/pkg.sh) prefer binstall over a source build.
 *
 * Was modules/rust.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/fetch.h"

#include <stddef.h>
#include <stdlib.h>

/* install_rustup -- the upstream installer, piped into a shell as OSR_USER.
 * --no-modify-path: the shell layers own $PATH (§5), and rustup appending its
 * own line to .profile would be a second manager of the same thing. */
static int install_rustup(void *ctx) {
    char *argv[9];
    (void)ctx;
    argv[0] = (char *)"sh";   argv[1] = (char *)"-s"; argv[2] = (char *)"--";
    argv[3] = (char *)"-y";   argv[4] = (char *)"--default-toolchain";
    argv[5] = (char *)"stable"; argv[6] = (char *)"--profile";
    argv[7] = (char *)"minimal"; argv[8] = NULL;
    {
        char *full[11];
        size_t n;
        for (n = 0; argv[n] != NULL; n++) full[n] = argv[n];
        full[n++] = (char *)"--no-modify-path";
        full[n] = NULL;
        if (!osr_fetch_pipe_user("https://sh.rustup.rs", full))
            osr_die("rustup install failed");
    }
    return 1;
}

/* user_x -- `as_user test -x <path>`: the probe has to be made AS the account,
 * because root cannot assume it can see that home. */
static int user_x(const char *path) {
    char *argv[4];
    argv[0] = (char *)"test"; argv[1] = (char *)"-x"; argv[2] = (char *)path; argv[3] = NULL;
    return osr_run_user(argv) == 0;
}

static int install_cargo_tooling(void *ctx) {
    Str bin, binstall, update, cargo, shim_src, shim_dst;
    char *argv[6];
    int rc;
    (void)ctx;

    str_initv(&bin, &binstall, &update, &cargo, &shim_src, &shim_dst, (Str *)NULL);
    str_addzz(&bin, osr_mod_home(), "/.cargo/bin", (const char *)NULL);
    str_addzz(&binstall, str_text(&bin), "/cargo-binstall", (const char *)NULL);
    str_addzz(&update, str_text(&bin), "/cargo-install-update", (const char *)NULL);
    str_addzz(&cargo, str_text(&bin), "/cargo", (const char *)NULL);

    /* binstall first: a prebuilt binary where upstream ships one turns every
     * later cargo: row from a compile into a download. */
    if (!user_x(str_text(&binstall))) {
        argv[0] = (char *)"bash"; argv[1] = NULL;
        if (!osr_fetch_pipe_user("https://raw.githubusercontent.com/cargo-bins/"
                                 "cargo-binstall/main/install-from-binstall-release.sh",
                                 argv))
            osr_warn("cargo-binstall install failed - cargo: packages will build from source");
    }
    if (!user_x(str_text(&update))) {
        int rc_upd;
        if (user_x(str_text(&binstall))) {
            argv[0] = binstall.p; argv[1] = (char *)"--no-confirm";
            argv[2] = (char *)"cargo-update"; argv[3] = NULL;
        } else {
            argv[0] = cargo.p; argv[1] = (char *)"install"; argv[2] = (char *)"--locked";
            argv[3] = (char *)"cargo-update"; argv[4] = NULL;
        }
        rc_upd = osr_run_user(argv);
        if (rc_upd != 0) osr_warn("cargo-update install failed");
    }
    /* The shim 20-aliases.zsh's cargo() hands to `cargo install-update -r`:
     * cargo-update invokes it as `<shim> install ...` and it rewrites that into
     * a binstall call. Dotfiles-owned (§5) - it is a flag translation, not a
     * setting, so it ships as a file rather than being generated here. */
    str_addzz(&shim_src, osr_mod_dotfiles(), "/cargo/cargo-binstall-shim", (const char *)NULL);
    str_addzz(&shim_dst, osr_mod_home(), "/.local/bin/cargo-binstall-shim", (const char *)NULL);
    (void)osr_install_layer(str_text(&shim_src), str_text(&shim_dst));
    argv[0] = (char *)"chmod"; argv[1] = (char *)"0755"; argv[2] = shim_dst.p; argv[3] = NULL;
    rc = osr_run_user(argv);

    str_freev(&bin, &binstall, &update, &cargo, &shim_src, &shim_dst, (Str *)NULL);
    /* The step's verdict is this chmod's, which is what the sh function's
     * status was: an unexecutable shim is a cargo() alias that silently does
     * nothing, so it is worth failing the step over. */
    return rc == 0;
}

int osrm_rust(void) {
    static const char *const deps[] = { "build", "curl", NULL };
    static const char *const native[] = { "build", "curl", "rust", NULL };
    Str cargo;
    int ok;

    /* No theme layer anywhere in a toolchain, so nothing for a theme pass to
     * do -- and plenty for it to break: rustup and cargo are plain commands,
     * which theme-only does not neutralise the way it does the package verbs.
     * The guard lives here rather than in the module table because `serie`
     * calls this directly, as its cargo: row's prerequisite. */
    if (osr_theme_only()) return 1;

    /* Termux: rustup refuses to run at all here (it compares $HOME against the
     * euid home -- one Android app uid, no passwd entry -- and bails before it
     * writes settings.toml), and cargo-binstall's release asset is a
     * *-unknown-linux-gnu binary that bionic cannot start. termux-main packages
     * the toolchain natively, so the whole rustup/binstall path is replaced by
     * one pkg row (see the bionic note in lib/pkgmap/termux.map). */
    if (osr_on_termux())
        return osr_pkg_install_step("Installing Rust (native package)", native);

    ok = osr_pkg_install_step("Installing build tools (cc, curl)", deps);

    str_init(&cargo);
    str_addzz(&cargo, osr_mod_home(), "/.cargo/bin/cargo", (const char *)NULL);
    if (user_x(str_text(&cargo))) {
        osr_infof("Rust already installed (%s) - skipping", str_text(&cargo));
    } else {
        ok = osr_step("Installing Rust via rustup", install_rustup, NULL) && ok;
    }
    str_free(&cargo);
    return osr_step("Installing cargo-binstall + cargo-update",
                    install_cargo_tooling, NULL) && ok;
}
