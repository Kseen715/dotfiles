# modules/foot.sh — foot terminal + JetBrains Mono Nerd Font + layered config.
# ONE copy, POSIX, distro-agnostic (was linux-rhel/modules/foot.sh, bash). The
# package goes through pkg_install/pkgmap; the font is a best-effort cosmetic
# asset (warn, never fail a run); config is split by ownership (§5):
#
#   foot.ini          dotfiles-owned (10-layer) — overwritten on update
#   foot-colors.ini   rice-owned theme (90-layer) — swapped on rice switch (§6),
#                     falling back to the dotfiles default when a rice ships none
#
# foot.ini carries `include=~/.config/foot/foot-colors.ini`, so the palette layer
# swaps independently of the base config — the §5 split applied to a DE config.
#
# The Nerd Font install is the shared, best-effort lib/fonts.sh helper (also used
# by starship/wezterm) — one copy of the download-unzip-register logic.

run_step "Installing foot terminal" pkg_install foot unzip fontconfig
run_step "Installing JetBrains Mono Nerd Font" osr_install_nerd_font JetBrainsMono

# Base config (dotfiles-owned, overwrite-on-update §5).
install_layer "$OSR_DOTFILES/foot/foot.ini" "$OSR_HOME/.config/foot/foot.ini"

# Palette (rice-owned theme, swapped on switch §6). Rice override wins; the
# dotfiles default covers a rice that ships no palette.
if [ -f "$OSR_RICE_DIR/config/foot/foot-colors.ini" ]; then
    install_layer "$OSR_RICE_DIR/config/foot/foot-colors.ini" "$OSR_HOME/.config/foot/foot-colors.ini"
elif [ -f "$OSR_DOTFILES/foot/foot-colors.ini" ]; then
    install_layer "$OSR_DOTFILES/foot/foot-colors.ini" "$OSR_HOME/.config/foot/foot-colors.ini"
fi
