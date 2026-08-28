# test/ref/htop_sh_ref.sh — the sh implementation of modules/htop.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/htop.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/htop.sh — htop process viewer. ONE copy, POSIX, distro-agnostic
# (was .../apps/htop.sh). Native everywhere, no config.
run_step "Installing htop" pkg_install htop
