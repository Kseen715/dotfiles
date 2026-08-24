# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/htop.sh — htop process viewer. ONE copy, POSIX, distro-agnostic
# (was .../apps/htop.sh). Native everywhere, no config.
run_step "Installing htop" pkg_install htop
