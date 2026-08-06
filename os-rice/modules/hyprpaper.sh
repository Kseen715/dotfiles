# session: wayland
# modules/hyprpaper.sh — hyprpaper wallpaper daemon + config. ONE copy, POSIX
# (was .../modules/hyprpaper.sh). Setting the live wallpaper is apply_wallpaper's
# job (§6); this module installs the daemon and its rice-owned config.
#
# hyprpaper.conf's `preload`/`wallpaper` need a real path, and hyprpaper does NOT
# read the session's env - the legacy config's bare `$WALLPAPER_PATH` resolved
# against nothing. It is a {{WALLPAPER_PATH}} placeholder now, filled by
# install_wallpaper_layer with the same installed path gtklock and hyprland's
# `env =` line get, so the daemon, the locker and the session agree on one file.
run_step "Installing hyprpaper" pkg_install hyprpaper
if [ -n "$OSR_RICE_DIR" ] && [ -f "$OSR_RICE_DIR/config/hypr/hyprpaper.conf" ]; then
    install_wallpaper_layer "$OSR_RICE_DIR/config/hypr/hyprpaper.conf" \
        "$OSR_HOME/.config/hypr/hyprpaper.conf"
fi
