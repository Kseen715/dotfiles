# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/inxi.sh — inxi system information tool. ONE copy, POSIX,
# distro-agnostic (was linux-debian/modules/inxi.sh). Native on every target.

run_step "Installing inxi" pkg_install inxi
