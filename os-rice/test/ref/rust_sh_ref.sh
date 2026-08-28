# test/ref/rust_sh_ref.sh — the sh implementation of modules/rust.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/rust.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/rust.sh — Rust toolchain via rustup. ONE copy, POSIX, distro-agnostic
# (was linux-rhel/modules/rust.sh, bash + hand-rolled `sudo -u`). The compiler +
# curl come from pkg_install (`build` maps per distro through pkgmap); rustup
# itself installs into OSR_USER's ~/.cargo as a user-space toolchain (§8). All
# user work runs through as_user — never a hand-rolled `sudo -u` + chown (the
# source of the arch drift bug, §7).
#
# This is a prerequisite module: list it in a rice BEFORE any cargo: row (manifest
# order is the dependency graph, §4). ~/.cargo/bin is added to PATH by the
# dotfiles 00-env.zsh layer (guard-style, §5) — this module does not touch PATH.

# osr_install_rustup — stream sh.rustup.rs to sh as OSR_USER, unattended.
# --no-modify-path: PATH is owned by the 00-env layer, not rustup's rc edits (§5).
osr_install_rustup() {
    osr_fetch_stdout https://sh.rustup.rs \
        | as_user sh -s -- -y --default-toolchain stable --profile minimal --no-modify-path
    check_error $? "rustup install failed"
}

# osr_install_cargo_tooling — cargo-binstall + cargo-update, the pair that turns
# every later cargo: row into a prebuilt-binary download instead of a source
# build. binstall comes from its own release script (installing it FROM source
# would defeat the point), and cargo-update then rides binstall. Best-effort
# throughout: on failure plain `cargo install` still works, so warn, never error.
osr_install_cargo_tooling() {
    _rust_bin="$OSR_HOME/.cargo/bin"
    if ! as_user test -x "$_rust_bin/cargo-binstall"; then
        osr_fetch_stdout https://raw.githubusercontent.com/cargo-bins/cargo-binstall/main/install-from-binstall-release.sh \
            | as_user bash \
            || warn "cargo-binstall install failed - cargo: packages will build from source"
    fi
    if ! as_user test -x "$_rust_bin/cargo-install-update"; then
        if as_user test -x "$_rust_bin/cargo-binstall"; then
            as_user "$_rust_bin/cargo-binstall" --no-confirm cargo-update
        else
            as_user "$_rust_bin/cargo" install --locked cargo-update
        fi || warn "cargo-update install failed"
    fi
    # The shim 20-aliases.zsh's cargo() hands to `cargo install-update -r`:
    # cargo-update invokes it as `<shim> install ...` and it rewrites that into a
    # binstall call. Dotfiles-owned (§5) — it is a flag translation, not a
    # setting, so it ships as a file rather than being generated here.
    install_layer "$OSR_DOTFILES/cargo/cargo-binstall-shim" "$OSR_HOME/.local/bin/cargo-binstall-shim"
    as_user chmod 0755 "$OSR_HOME/.local/bin/cargo-binstall-shim"
}

run_step "Installing build tools (cc, curl)" pkg_install build curl

# Idempotency probe (§2): rustup drops cargo at ~/.cargo/bin. If it is already
# there, converge silently — no network round-trip, so a second run all-skips.
_rust_cargo="$OSR_HOME/.cargo/bin/cargo"
if as_user test -x "$_rust_cargo"; then
    info "Rust already installed ($_rust_cargo) - skipping"
else
    run_step "Installing Rust via rustup" osr_install_rustup
fi

# Runs on every pass: internally idempotent, and it is what makes the cargo:
# provider (lib/pkg.sh) prefer binstall over a source build.
run_step "Installing cargo-binstall + cargo-update" osr_install_cargo_tooling
