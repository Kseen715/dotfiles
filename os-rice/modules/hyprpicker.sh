# session: wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/hyprpicker.sh — hyprpicker color picker. ONE copy, POSIX
# (was .../modules/hyprpicker.sh). Native on Arch, no config.
run_step "Installing hyprpicker" pkg_install hyprpicker
