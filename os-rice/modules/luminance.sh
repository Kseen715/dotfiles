# session: wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/luminance.sh — Luminance brightness controller (AUR). ONE copy, POSIX
# (was .../modules/luminance.sh).
run_step "Installing Luminance (AUR)" pkg_install luminance
