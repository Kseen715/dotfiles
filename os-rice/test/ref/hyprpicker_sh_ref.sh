# test/ref/hyprpicker_sh_ref.sh — the sh implementation of modules/hyprpicker.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/hyprpicker.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: wayland
# modules/hyprpicker.sh — hyprpicker color picker. ONE copy, POSIX
# (was .../modules/hyprpicker.sh). Native on Arch, no config.
run_step "Installing hyprpicker" pkg_install hyprpicker
