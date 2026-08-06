# session: wayland
# modules/swaylock.sh — swaylock screen locker + rice-owned config. ONE copy,
# POSIX (was .../modules/swaylock.sh). Alternative locker (gtklock is default);
# kept as an available module.
run_step "Installing swaylock" pkg_install swaylock
if [ -n "$OSR_RICE_DIR" ] && [ -f "$OSR_RICE_DIR/config/swaylock/config" ]; then
    install_layer "$OSR_RICE_DIR/config/swaylock/config" "$OSR_HOME/.config/swaylock/config"
fi
