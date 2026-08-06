# session: x11+wayland
# modules/htop.sh — htop process viewer. ONE copy, POSIX, distro-agnostic
# (was .../apps/htop.sh). Native everywhere, no config.
run_step "Installing htop" pkg_install htop
