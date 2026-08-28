# test/ref/hypridle_sh_ref.sh — the sh implementation of modules/hypridle.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/hypridle.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: wayland
# themable: yes
# modules/hypridle.sh — hypridle idle daemon + config. ONE copy, POSIX
# (was .../modules/hypridle.sh).
run_step "Installing hypridle" pkg_install hypridle
if [ -n "$OSR_THEME_DIR" ] && [ -f "$OSR_THEME_DIR/config/hypr/hypridle.conf" ]; then
    install_layer "$OSR_THEME_DIR/config/hypr/hypridle.conf" "$OSR_HOME/.config/hypr/hypridle.conf"
fi
