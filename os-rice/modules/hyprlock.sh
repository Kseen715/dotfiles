# session: wayland
# themable: yes
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/hyprlock.sh — hyprlock screen locker + config. ONE copy, POSIX
# (was .../modules/hyprlock.sh).
run_step "Installing hyprlock" pkg_install hyprlock
if [ -n "$OSR_THEME_DIR" ] && [ -f "$OSR_THEME_DIR/config/hypr/hyprlock.conf" ]; then
    install_layer "$OSR_THEME_DIR/config/hypr/hyprlock.conf" "$OSR_HOME/.config/hypr/hyprlock.conf"
fi
