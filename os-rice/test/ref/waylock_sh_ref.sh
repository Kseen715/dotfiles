# test/ref/waylock_sh_ref.sh — the sh implementation of modules/waylock.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/waylock.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: wayland
# themable: yes
# modules/waylock.sh — waylock minimal screen locker + rice-owned config. ONE
# copy, POSIX (was .../modules/waylock.sh). Alternative locker; available module.
run_step "Installing waylock" pkg_install waylock
if [ -n "$OSR_THEME_DIR" ] && [ -f "$OSR_THEME_DIR/config/waylock/waylock.toml" ]; then
    install_layer "$OSR_THEME_DIR/config/waylock/waylock.toml" "$OSR_HOME/.config/waylock/waylock.toml"
fi
