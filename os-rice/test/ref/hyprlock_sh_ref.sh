# test/ref/hyprlock_sh_ref.sh — the sh implementation of modules/hyprlock.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/hyprlock.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: wayland
# themable: yes
# modules/hyprlock.sh — hyprlock screen locker + config. ONE copy, POSIX
# (was .../modules/hyprlock.sh).
run_step "Installing hyprlock" pkg_install hyprlock
if [ -n "$OSR_THEME_DIR" ] && [ -f "$OSR_THEME_DIR/config/hypr/hyprlock.conf" ]; then
    install_layer "$OSR_THEME_DIR/config/hypr/hyprlock.conf" "$OSR_HOME/.config/hypr/hyprlock.conf"
fi
