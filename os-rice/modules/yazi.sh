# session: x11+wayland
# modules/yazi.sh — Yazi file manager + layered config. ONE copy, POSIX,
# distro-agnostic. Config split by ownership (§5):
#
#   yazi.toml     dotfiles-owned (10) — keymaps/manager settings, rice-independent
#   package.toml  dotfiles-owned (10) — declared plugin/flavor set
#   flavors/      dotfiles-owned       — installed flavor programs (G5: not config)
#   theme.toml    rice-owned theme (90) — selects the flavor, swapped on switch
#                 (§6); falls back to the dotfiles default when a rice ships none.

# chafa is yazi's last-resort image adapter, and on several of these hosts it is
# the ONLY one. Yazi's ladder is: kitty graphics protocol -> iTerm2 inline images
# -> sixel -> Überzug++ -> chafa (unicode half-blocks). Ghostty and foot cover
# the top of that ladder; Alacritty and xterm support no protocol at all, and
# Überzug++ is in no Debian/Fedora archive - so without chafa on PATH an image
# preview is simply blank. Yazi picks it up by presence, with no config key to
# set, which is the whole wiring. Small (~1 MB) and harmless where a real
# protocol wins the detection.
run_step "Installing Yazi" pkg_install yazi chafa

# ...but presence is not sufficiency: yazi invokes chafa with --probe, which only
# exists in 1.16.0+ (CHAFA_MIN, lib/build.sh), and an older one exits on the
# unknown option leaving the preview pane blank with no error anywhere. The
# pkgmap rows route the releases known to be behind straight to provide_chafa,
# and this catches the rest - a box that ALREADY had an old distro chafa (which
# satisfies pkg_install's presence probe and would never be replaced), an EOL
# release, or an admin-pinned package. provide_chafa is a no-op when the chafa
# on PATH is already new enough, so the guard costs one `chafa --version`.
if ! _chafa_ok; then
    run_step "Building chafa >= $CHAFA_MIN (yazi image previews)" provide_chafa
fi

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
