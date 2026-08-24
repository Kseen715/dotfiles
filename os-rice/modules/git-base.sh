# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/git-base.sh — git + core CLI tools (git, wget, editors, man). The base
# every later module and the user relies on. Native everywhere; ONE copy, POSIX
# (was linux-arch-x86_64-hyprland-glass/modules/git.sh, bash+pacman).
run_step "Installing git and base CLI tools" pkg_install git wget nano vim man-db
