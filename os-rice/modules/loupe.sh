# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/loupe.sh — Loupe image viewer. ONE copy, POSIX
# (was .../modules/loupe.sh). Native, no config. Available module.
run_step "Installing Loupe" pkg_install loupe
