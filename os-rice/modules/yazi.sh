# session: x11+wayland
# modules/yazi.sh — Yazi file manager + layered config. ONE copy, POSIX,
# distro-agnostic. Config split by ownership (§5):
#
#   yazi.toml     dotfiles-owned (10) — keymaps/manager settings, rice-independent
#   package.toml  dotfiles-owned (10) — declared plugin/flavor set
#   flavors/      dotfiles-owned       — installed flavor programs (G5: not config)
#   theme.toml    rice-owned theme (90) — selects the flavor, swapped on switch
#                 (§6); falls back to the dotfiles default when a rice ships none.

# Yazi's adapter ladder: kitty graphics protocol -> iTerm2 inline images ->
# sixel -> Überzug++ -> chafa (unicode half-blocks). Ghostty and foot cover the
# top of it; Alacritty and xterm speak no protocol at all and fall through.
#
# Where they fall through to is decided by the SESSION, not by what is installed
# (yazi-adapter/src/drivers/drivers.rs): on X11 yazi returns the Überzug++ driver
# UNCONDITIONALLY - no compositor check, no chafa fallback - and on Wayland it
# does the same when the compositor is sway/Hyprland/niri/Wayfire. chafa is
# reached only with no graphical session (SSH, tmux on a server) or on a Wayland
# compositor yazi does not support. So chafa is the headless safety net, and
# Überzug++ below is what makes previews work on a desktop.
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

# _yazi_needs_ueberzug — mirrors Drivers::matches() in yazi, in its order, so the
# install decision matches what yazi will actually pick at runtime. Returns false
# on a headless box (container, SSH, the test matrix), where nothing routes to
# Überzug++ and chafa is the adapter - so no desktop-only build happens there.
_yazi_needs_ueberzug() {
    # The four compositors yazi's Ueberzug::supported_compositor() accepts.
    _yz_wl=''
    for _yz_v in "${NIRI_SOCKET:-}" "${SWAYSOCK:-}" \
                 "${HYPRLAND_INSTANCE_SIGNATURE:-}" "${WAYFIRE_SOCKET:-}"; do
        [ -n "$_yz_v" ] && _yz_wl=1
    done
    case "${XDG_SESSION_TYPE:-}" in
        x11)     return 0 ;;
        wayland) [ -n "$_yz_wl" ]; return $? ;;
    esac
    if [ -n "${WAYLAND_DISPLAY:-}" ]; then
        [ -n "$_yz_wl" ]; return $?
    fi
    [ -n "${DISPLAY:-}" ]
}

# Überzug++ draws real pixels in a terminal that has no graphics protocol of its
# own, which is the entire reason an image preview works under Alacritty on X11.
# Arch/Gentoo package it; everywhere else pkgmap routes to provide_ueberzugpp.
if _yazi_needs_ueberzug; then
    run_step "Installing Ueberzug++ (yazi image previews)" pkg_install ueberzugpp
fi

_yazi_cfg="$OSR_HOME/.config/yazi"

# Base config + declared flavor set (dotfiles-owned, overwrite-on-update §5).
if [ -f "$OSR_DOTFILES/yazi/yazi.toml" ]; then
    install_layer "$OSR_DOTFILES/yazi/yazi.toml" "$_yazi_cfg/yazi.toml"
fi
if [ -f "$OSR_DOTFILES/yazi/package.toml" ]; then
    install_layer "$OSR_DOTFILES/yazi/package.toml" "$_yazi_cfg/package.toml"
fi
# The flavor itself (theme-owned, §6b). yazi wants a DIRECTORY per flavor -
# flavor.toml plus the tmtheme.xml that colors file previews - so the theme
# renders one named after itself. Five vendored flavor trees used to live in
# dotfiles/yazi/flavors/ and only the four themes that had one were painted;
# now every theme has both files, from the same palette its terminal uses.
if [ -n "${OSR_THEME:-}" ]; then
    _yazi_fl="$_yazi_cfg/flavors/$OSR_THEME.yazi"
    as_user mkdir -p "$_yazi_fl"
    install_theme_layer yazi flavor.toml "$_yazi_fl/flavor.toml" || :
    install_theme_layer yazi tmtheme.xml "$_yazi_fl/tmtheme.xml" || :
fi

# ...and the one-line file that selects it.
if install_theme_layer yazi theme.toml "$_yazi_cfg/theme.toml"; then
    :
elif [ -f "$OSR_DOTFILES/yazi/theme.toml" ]; then
    install_layer "$OSR_DOTFILES/yazi/theme.toml" "$_yazi_cfg/theme.toml"
fi
