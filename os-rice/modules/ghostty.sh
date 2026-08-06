# session: x11+wayland
# modules/ghostty.sh — Ghostty terminal + JetBrains Mono Nerd Font + layered
# config. ONE copy, POSIX, distro-agnostic (was linux-debian/modules/ghostty.sh,
# a from-source Zig build). Native-first: native on arch/void and recent Ubuntu;
# elsewhere a community binary (Fedora COPR, ghostty-ubuntu .deb) and, as the
# last resort, built from source with a bootstrapped Zig toolchain
# (source:provide_ghostty via pkgmap). The source build is heavy (a full Zig
# compile) and is a real-desktop concern (§9), not container-tested.
#
# Config is split by ownership (§5), same shape as foot:
#
#   config          dotfiles-owned (10-layer) — overwritten on update; carries
#                   the ssh-comfort settings (terminfo, OSC 52 clipboard) and
#                   the 0.75 transparency
#   ghostty-theme   rice-owned palette (90-layer) — swapped on rice switch (§6),
#                   falling back to the dotfiles default when a rice ships none
#
# `config` ends with `config-file = ?ghostty-theme`, so the palette layer swaps
# independently of the base — the §5 split applied to a DE config. The '?' keeps
# a missing palette from being a startup error.
#
# The Nerd Font install is the shared, best-effort lib/fonts.sh helper (also used
# by foot/starship/wezterm) — one copy of the download-unzip-register logic.

run_step "Installing Ghostty" pkg_install ghostty unzip fontconfig
run_step "Installing JetBrains Mono Nerd Font" osr_install_nerd_font JetBrainsMono

# Base config (dotfiles-owned, overwrite-on-update §5).
if [ -f "$OSR_DOTFILES/ghostty/config" ]; then
    install_layer "$OSR_DOTFILES/ghostty/config" "$OSR_HOME/.config/ghostty/config"
fi

# Palette (rice-owned theme, swapped on switch §6). Rice override wins; the
# dotfiles default covers a rice that ships no palette. In --module mode
# OSR_RICE_DIR is whatever rice the theme picker resolved (§6).
if [ -n "${OSR_RICE_DIR:-}" ] && [ -f "$OSR_RICE_DIR/config/ghostty/ghostty-theme" ]; then
    install_layer "$OSR_RICE_DIR/config/ghostty/ghostty-theme" "$OSR_HOME/.config/ghostty/ghostty-theme"
elif [ -f "$OSR_DOTFILES/ghostty/ghostty-theme" ]; then
    install_layer "$OSR_DOTFILES/ghostty/ghostty-theme" "$OSR_HOME/.config/ghostty/ghostty-theme"
fi
