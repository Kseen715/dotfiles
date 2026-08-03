# modules/wezterm.sh — WezTerm terminal + JetBrains Mono Nerd Font + layered
# config. ONE copy, POSIX, distro-agnostic. WezTerm is BUILT FROM SOURCE on every
# target (any.map -> source:provide_wezterm, upstream's documented route); there
# is no AppImage/flatpak path. The build needs a Rust toolchain, so list `rust`
# before `wezterm` in a rice (manifest order is the dependency graph, §4).
#
# Config is split by ownership (§5), same shape as foot/ghostty:
#
#   .wezterm.lua           dotfiles-owned (10-layer) — overwritten on update
#   colors/osr-rice.toml   rice-owned palette (90-layer) — swapped on rice switch
#                          (§6), falling back to the dotfiles default when a rice
#                          ships none
#
# The base .wezterm.lua selects `color_scheme = "osr-rice"` when that file is
# present, so the palette swaps independently of the base — the §5 split applied
# to a DE config, via WezTerm's own custom-color-scheme directory (its config is
# Lua and has no include directive like foot.ini).
#
# The Nerd Font install is the shared, best-effort lib/fonts.sh helper (also used
# by foot/ghostty/starship) — one copy of the download-unzip-register logic.

run_step "Installing WezTerm (source build)" pkg_install wezterm unzip fontconfig
run_step "Installing JetBrains Mono Nerd Font" osr_install_nerd_font JetBrainsMono

# Base config (dotfiles-owned, overwrite-on-update §5).
if [ -f "$OSR_DOTFILES/wezterm/.wezterm.lua" ]; then
    install_layer "$OSR_DOTFILES/wezterm/.wezterm.lua" "$OSR_HOME/.wezterm.lua"
fi

# Palette (rice-owned theme, swapped on switch §6). Rice override wins; the
# dotfiles default covers a rice that ships no palette. In --module mode
# OSR_RICE_DIR is whatever rice the theme picker resolved (§6).
_wt_colors="$OSR_HOME/.config/wezterm/colors/osr-rice.toml"
if [ -n "${OSR_RICE_DIR:-}" ] && [ -f "$OSR_RICE_DIR/config/wezterm/wezterm-theme.toml" ]; then
    install_layer "$OSR_RICE_DIR/config/wezterm/wezterm-theme.toml" "$_wt_colors"
elif [ -f "$OSR_DOTFILES/wezterm/wezterm-theme.toml" ]; then
    install_layer "$OSR_DOTFILES/wezterm/wezterm-theme.toml" "$_wt_colors"
fi
