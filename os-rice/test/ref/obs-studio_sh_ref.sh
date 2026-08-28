# test/ref/obs-studio_sh_ref.sh — the sh implementation of modules/obs-studio.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/obs-studio.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/obs-studio.sh — OBS Studio. ONE copy, POSIX (was .../apps/obs-studio.sh).
run_step "Installing OBS Studio" pkg_install obs-studio
as_user mkdir -p "$OSR_HOME/.config/obs-studio"
