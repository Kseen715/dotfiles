# test/ref/helvum_sh_ref.sh — the sh implementation of modules/helvum.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/helvum.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/helvum.sh — Helvum PipeWire patchbay (GTK). ONE copy, POSIX
# (was .../modules/helvum.sh). Native, no config. Available module (qpwgraph is
# the default patchbay in this rice).
run_step "Installing Helvum" pkg_install helvum
