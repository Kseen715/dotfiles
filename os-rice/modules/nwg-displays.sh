# session: wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/nwg-displays.sh — nwg-displays monitor layout tool. ONE copy, POSIX
# (was .../modules/nwg-displays.sh). Native, no config.
run_step "Installing nwg-displays" pkg_install nwg-displays
