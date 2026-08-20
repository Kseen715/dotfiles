# session: wayland
# themable: yes
# modules/hypridle.sh — hypridle idle daemon + config. ONE copy, POSIX
# (was .../modules/hypridle.sh).
run_step "Installing hypridle" pkg_install hypridle
if [ -n "$OSR_THEME_DIR" ] && [ -f "$OSR_THEME_DIR/config/hypr/hypridle.conf" ]; then
    install_layer "$OSR_THEME_DIR/config/hypr/hypridle.conf" "$OSR_HOME/.config/hypr/hypridle.conf"
fi
