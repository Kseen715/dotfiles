# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/helvum.sh — Helvum PipeWire patchbay (GTK). ONE copy, POSIX
# (was .../modules/helvum.sh). Native, no config. Available module (qpwgraph is
# the default patchbay in this rice).
run_step "Installing Helvum" pkg_install helvum
