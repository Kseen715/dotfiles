# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/obs-studio.sh — OBS Studio. ONE copy, POSIX (was .../apps/obs-studio.sh).
run_step "Installing OBS Studio" pkg_install obs-studio
as_user mkdir -p "$OSR_HOME/.config/obs-studio"
