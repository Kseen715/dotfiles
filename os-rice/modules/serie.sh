# modules/serie.sh — serie, a rich git-commit-graph TUI. ONE copy, POSIX,
# distro-agnostic (was linux-debian/modules/serie.sh). Native on arch/alpine;
# everywhere else it is installed from crates.io via the cargo: provider, so the
# Rust toolchain is a prerequisite and is installed here first (manifest order
# is the dependency graph, §4).

# Ensure a toolchain exists before any cargo: resolution (no-op if serie is
# native on this distro, but harmless — rust is idempotent).
case "$(_pkgmap_one serie)" in
    cargo:*) . "$OSR_ROOT/modules/rust.sh" ;;
esac

run_step "Installing serie" pkg_install serie
