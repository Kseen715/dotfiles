# session: x11+wayland
# modules/btop.sh — btop resource monitor + dotfiles config. ONE copy, POSIX,
# distro-agnostic (was linux-debian/modules/btop.sh). Native on every target
# except Debian 11 (bullseye), which gets the upstream static binary via a facet
# row in apt.map. Config is split by ownership (§5), same shape as foot/ghostty:
#
#   btop.conf    dotfiles-owned (10-layer) — overwritten on update
#   btop.theme   rice-owned palette (90-layer) — swapped on rice switch (§6),
#                falling back to the dotfiles default when a rice ships none
#
# btop.conf carries `color_theme = "rice"`, which btop resolves to the theme
# named rice.theme in ~/.config/btop/themes — so the palette layer swaps
# independently of the base config.
run_step "Installing btop" pkg_install btop
if [ -f "$OSR_DOTFILES/btop/btop.conf" ]; then
    install_layer "$OSR_DOTFILES/btop/btop.conf" "$OSR_HOME/.config/btop/btop.conf"
fi

# Palette (rice-owned theme, swapped on switch §6). Rice override wins; the
# dotfiles default covers a rice that ships no palette. In --module mode
# OSR_THEME_DIR is whatever rice the theme picker resolved (§6).
if [ -n "${OSR_THEME_DIR:-}" ] && [ -f "$OSR_THEME_DIR/config/btop/btop.theme" ]; then
    install_layer "$OSR_THEME_DIR/config/btop/btop.theme" "$OSR_HOME/.config/btop/themes/rice.theme"
elif [ -f "$OSR_DOTFILES/btop/btop.theme" ]; then
    install_layer "$OSR_DOTFILES/btop/btop.theme" "$OSR_HOME/.config/btop/themes/rice.theme"
fi
