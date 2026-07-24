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

run_step "Installing build tools (cc, curl)" pkg_install build curl

# Idempotency probe (§2): rustup drops cargo at ~/.cargo/bin. If it is already
# there, converge silently — no network round-trip, so a second run all-skips.
_rust_cargo="$OSR_HOME/.cargo/bin/cargo"
if as_user test -x "$_rust_cargo"; then
    info "Rust already installed ($_rust_cargo) - skipping"
else
    run_step "Installing Rust via rustup" osr_install_rustup
fi
