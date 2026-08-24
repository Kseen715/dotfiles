# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/nautilus.sh — GNOME Files (Nautilus) file manager. ONE copy, POSIX
# (was .../modules/nautilus.sh). Native, no config.
run_step "Installing Nautilus" pkg_install nautilus
