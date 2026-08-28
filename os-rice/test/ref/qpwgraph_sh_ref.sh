# test/ref/qpwgraph_sh_ref.sh — the sh implementation of modules/qpwgraph.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/qpwgraph.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/qpwgraph.sh — qpwgraph PipeWire patchbay. ONE copy, POSIX
# (was .../modules/qpwgraph.sh). Native, no config.
run_step "Installing qpwgraph" pkg_install qpwgraph
