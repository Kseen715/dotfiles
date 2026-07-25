# modules/yazi.sh — Yazi file manager + layered config. ONE copy, POSIX,
# distro-agnostic. Config split by ownership (§5):
#
#   yazi.toml     dotfiles-owned (10) — keymaps/manager settings, rice-independent
#   package.toml  dotfiles-owned (10) — declared plugin/flavor set
#   flavors/      dotfiles-owned       — installed flavor programs (G5: not config)
#   theme.toml    rice-owned theme (90) — selects the flavor, swapped on switch
#                 (§6); falls back to the dotfiles default when a rice ships none.

run_step "Installing Yazi" pkg_install yazi

_yazi_cfg="$OSR_HOME/.config/yazi"

# Base config + declared flavor set (dotfiles-owned, overwrite-on-update §5).
if [ -f "$OSR_DOTFILES/yazi/yazi.toml" ]; then
    install_layer "$OSR_DOTFILES/yazi/yazi.toml" "$_yazi_cfg/yazi.toml"
fi
if [ -f "$OSR_DOTFILES/yazi/package.toml" ]; then
    install_layer "$OSR_DOTFILES/yazi/package.toml" "$_yazi_cfg/package.toml"
fi
if [ -d "$OSR_DOTFILES/yazi/flavors" ]; then
    as_user mkdir -p "$_yazi_cfg/flavors"
    as_user cp -rf "$OSR_DOTFILES/yazi/flavors/." "$_yazi_cfg/flavors/"
fi

# Flavor selection is the rice-owned theme (§6): rice override wins, dotfiles
# default covers a rice that ships none.
if [ -n "${OSR_RICE_DIR:-}" ] && [ -f "$OSR_RICE_DIR/config/yazi/theme.toml" ]; then
    install_layer "$OSR_RICE_DIR/config/yazi/theme.toml" "$_yazi_cfg/theme.toml"
elif [ -f "$OSR_DOTFILES/yazi/theme.toml" ]; then
    install_layer "$OSR_DOTFILES/yazi/theme.toml" "$_yazi_cfg/theme.toml"
fi
