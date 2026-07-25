# modules/hyprlock.sh — hyprlock screen locker + config. ONE copy, POSIX
# (was .../modules/hyprlock.sh).
run_step "Installing hyprlock" pkg_install hyprlock
if [ -n "$OSR_RICE_DIR" ] && [ -f "$OSR_RICE_DIR/config/hypr/hyprlock.conf" ]; then
    install_layer "$OSR_RICE_DIR/config/hypr/hyprlock.conf" "$OSR_HOME/.config/hypr/hyprlock.conf"
fi
