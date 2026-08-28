# test/ref/celluloid_sh_ref.sh — the sh implementation of modules/celluloid.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/celluloid.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/celluloid.sh — Celluloid (mpv GTK frontend). ONE copy, POSIX
# (was .../apps/celluloid.sh). Native, no config.
run_step "Installing Celluloid" pkg_install celluloid
