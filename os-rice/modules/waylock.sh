# session: wayland
# themable: yes
# modules/waylock.sh — waylock minimal screen locker + rice-owned config. ONE
# copy, POSIX (was .../modules/waylock.sh). Alternative locker; available module.
run_step "Installing waylock" pkg_install waylock
if [ -n "$OSR_THEME_DIR" ] && [ -f "$OSR_THEME_DIR/config/waylock/waylock.toml" ]; then
    install_layer "$OSR_THEME_DIR/config/waylock/waylock.toml" "$OSR_HOME/.config/waylock/waylock.toml"
fi
