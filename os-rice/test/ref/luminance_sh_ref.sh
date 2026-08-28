# test/ref/luminance_sh_ref.sh — the sh implementation of modules/luminance.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/luminance.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: wayland
# modules/luminance.sh — Luminance brightness controller (AUR). ONE copy, POSIX
# (was .../modules/luminance.sh).
run_step "Installing Luminance (AUR)" pkg_install luminance
