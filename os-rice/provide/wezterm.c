/* provide/wezterm.c -- build WezTerm from source, the route upstream
 * documents (https://wezterm.org/install/source.html). Included by
 * provide_module.c; see provide_module.h for the builder contract and
 * osrp_* helpers.
 *
 * This is the C sibling of lib/build.sh's provide_wezterm, and deliberately
 * the same recipe: clone with submodules, then `cargo build --release`.
 * Heavy (a full Rust workspace compile), which is why it is NOT the route
 * everywhere on Windows the way it is on Linux -- windows.map sends x86_64
 * to winget and reaches this builder only for arm64, where upstream
 * publishes no build at all and so no manager has anything to offer.
 *
 * Prerequisites, from upstream's own page: Rust via rustup and specifically
 * the MSVC toolchain ("the only supported way to build wezterm"), git for
 * the clone, and Strawberry Perl, which OpenSSL's build needs on Windows.
 * They are installed here through windows.map rather than assumed, the same
 * way lib/build.sh's provide_wezterm opens with `pkg_install build git`.
 *
 * Idempotency is osr_provide_run's have_command probe, not this file's.
 */

/* wezterm_prepare_source -- get a buildable checkout into `src`, reusing one
 * that is already there.
 *
 * A failed build KEEPS the checkout, so a retry resumes instead of paying
 * for a second full compile after a transient network or registry blip --
 * the same contract as the sh builder. Only a successful install cleans up.
 */
static int wezterm_prepare_source(const char *src) {
    char cmd[OSRP_CMD_MAX];
    char marker[OSRP_PATH_MAX];

    if (!osrp_join(marker, sizeof(marker), src, "\\Cargo.toml", NULL)) return 0;

    if (osr_winbin_file_exists(marker)) {
        osr_info("reusing the existing wezterm checkout (%s) -- rebuild is incremental", src);
    } else {
        /* --branch=main is what upstream documents for a source build, and
         * the submodules (vendored freetype/harfbuzz/...) are not optional. */
        if (!osrp_join(cmd, sizeof(cmd),
                "git clone --depth=1 --branch=main --recursive "
                "https://github.com/wezterm/wezterm.git \"", src, "\"", NULL)) return 0;
        if (osr_run_step("cloning wezterm", cmd) != 0) {
            osr_warn("failed to clone wezterm");
            return 0;
        }
    }

    /* Re-run even for a reused checkout: a clone interrupted partway through
     * its submodules leaves a tree that looks complete but will not build. */
    if (!osrp_join(cmd, sizeof(cmd), "git -C \"", src, "\" submodule update --init --recursive",
                   NULL)) return 0;
    if (osr_run_step("wezterm submodules", cmd) != 0) {
        osr_warn("wezterm submodule checkout failed");
        return 0;
    }

    return 1;
}

static int provide_wezterm(const char *map_path, const char *name, const char *test_command) {
    /* wezterm.exe is the CLI, wezterm-gui.exe the terminal a user actually
     * launches, and the mux server is what `wezterm connect` talks to.
     * Upstream ships all three, so installing only the first would look
     * like success and behave like a broken install. */
    static const char *binaries[3] = { "wezterm.exe", "wezterm-gui.exe", "wezterm-mux-server.exe" };
    char src[OSRP_PATH_MAX];
    char bin_dir[OSRP_PATH_MAX];
    char built[OSRP_PATH_MAX];
    char cmd[OSRP_CMD_MAX];
    unsigned long i;
    int placed;

    (void)name;

    /* Build dependencies, through the map so their ids live in one file.
     * cargo rather than rustup as the probe: rustup is the installer, cargo
     * is what this build needs on PATH. */
    if (!osr_winpkg_install(map_path, "git", "git")) {
        osr_warn("wezterm needs git to fetch its source");
        return 0;
    }
    if (!osr_winpkg_install(map_path, "rustup", "cargo")) {
        osr_warn("wezterm needs the Rust toolchain (rustup) to build");
        return 0;
    }
    /* OpenSSL's build script shells out to perl on Windows; without it the
     * compile dies deep inside a dependency with an unhelpful message. */
    if (!osr_winpkg_install(map_path, "strawberryperl", "perl")) {
        osr_warn("wezterm needs Strawberry Perl to build OpenSSL");
        return 0;
    }

    if (!osr_winbin_src_dir("wezterm", src, sizeof(src))) return 0;
    if (!osr_winbin_bin_dir("wezterm", bin_dir, sizeof(bin_dir))) return 0;

    if (!wezterm_prepare_source(src)) return 0;

    /* The long pole: a full Rust workspace compile. --release because a
     * debug wezterm is unusably slow. */
    if (!osrp_join(cmd, sizeof(cmd), "cargo build --release --manifest-path \"", src,
                   "\\Cargo.toml\"", NULL)) return 0;
    if (osr_run_step("compiling wezterm (this takes a while)", cmd) != 0) {
        osr_warn("wezterm build failed (checkout kept at %s -- rerun to resume)", src);
        return 0;
    }

    placed = 0;
    for (i = 0; i < 3; i++) {
        if (!osrp_join(built, sizeof(built), src, "\\target\\release\\", binaries[i], NULL)) continue;
        if (!osr_winbin_file_exists(built)) continue;
        if (osr_winbin_place(built, bin_dir, binaries[i])) placed++;
    }

    if (placed == 0) {
        osr_warn("wezterm compiled but no binaries turned up in %s\\target\\release "
                 "(checkout kept -- rerun to resume)", src);
        return 0;
    }

    osr_winbin_add_to_path(bin_dir);

    if (!osr_winpkg_have_command(test_command)) {
        osr_warn("wezterm was built into %s but '%s' still does not resolve",
                 bin_dir, test_command);
        return 0;
    }

    /* Only a successful install throws the tree away. */
    if (osrp_join(cmd, sizeof(cmd), "rmdir /s /q \"", src, "\"", NULL)) {
        osr_run_step("removing the wezterm build tree", cmd);
    }

    osr_success("  %-14s built from source (%d binaries)", "wezterm", placed);
    return 1;
}
