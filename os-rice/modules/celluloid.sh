# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/celluloid.sh — Celluloid (mpv GTK frontend). ONE copy, POSIX
# (was .../apps/celluloid.sh). Native, no config.
run_step "Installing Celluloid" pkg_install celluloid
