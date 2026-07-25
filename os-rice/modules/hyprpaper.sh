# modules/hyprpaper.sh — hyprpaper wallpaper daemon + config. ONE copy, POSIX
# (was .../modules/hyprpaper.sh). The wallpaper itself is set by the framework's
# apply_wallpaper (§6); here we install the daemon and its rice-owned config.
run_step "Installing hyprpaper" pkg_install hyprpaper
if [ -n "$OSR_RICE_DIR" ] && [ -f "$OSR_RICE_DIR/config/hypr/hyprpaper.conf" ]; then
    install_layer "$OSR_RICE_DIR/config/hypr/hyprpaper.conf" "$OSR_HOME/.config/hypr/hyprpaper.conf"
fi
