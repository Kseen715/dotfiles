# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/qpwgraph.sh — qpwgraph PipeWire patchbay. ONE copy, POSIX
# (was .../modules/qpwgraph.sh). Native, no config.
run_step "Installing qpwgraph" pkg_install qpwgraph
