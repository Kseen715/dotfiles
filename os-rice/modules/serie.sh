# session: x11+wayland
# themable: yes
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

# config.toml is a pure palette here (§5): serie has no include mechanism and we
# ship no non-color settings, so the whole file is the rice-owned theme layer
# (90-*, swapped on rice switch, §6). Rice override wins; the dotfiles default
# covers a rice that ships none. In --module mode OSR_THEME_DIR is whatever rice
# the theme picker resolved (§6).
if install_theme_layer serie config.toml "$OSR_HOME/.config/serie/config.toml"; then
    :
elif [ -f "$OSR_DOTFILES/serie/config.toml" ]; then
    install_layer "$OSR_DOTFILES/serie/config.toml" "$OSR_HOME/.config/serie/config.toml"
fi
