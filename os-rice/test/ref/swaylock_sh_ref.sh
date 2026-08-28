# test/ref/swaylock_sh_ref.sh — the sh implementation of modules/swaylock.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/swaylock.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: wayland
# themable: yes
# modules/swaylock.sh — swaylock screen locker + rice-owned config. ONE copy,
# POSIX (was .../modules/swaylock.sh). Alternative locker (gtklock is default);
# kept as an available module.
run_step "Installing swaylock" pkg_install swaylock
if [ -n "$OSR_THEME_DIR" ] && [ -f "$OSR_THEME_DIR/config/swaylock/config" ]; then
    install_layer "$OSR_THEME_DIR/config/swaylock/config" "$OSR_HOME/.config/swaylock/config"
fi
