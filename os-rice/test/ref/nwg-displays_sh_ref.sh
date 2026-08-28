# test/ref/nwg-displays_sh_ref.sh — the sh implementation of modules/nwg-displays.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/nwg-displays.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: wayland
# modules/nwg-displays.sh — nwg-displays monitor layout tool. ONE copy, POSIX
# (was .../modules/nwg-displays.sh). Native, no config.
run_step "Installing nwg-displays" pkg_install nwg-displays
