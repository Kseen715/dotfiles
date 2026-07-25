# modules/obs-studio.sh — OBS Studio. ONE copy, POSIX (was .../apps/obs-studio.sh).
run_step "Installing OBS Studio" pkg_install obs-studio
as_user mkdir -p "$OSR_HOME/.config/obs-studio"
