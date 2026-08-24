# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/onlyoffice.sh — ONLYOFFICE Desktop Editors (AUR). ONE copy, POSIX
# (was .../apps/onlyoffice.sh). Available module (not in default rice.list).
run_step "Installing ONLYOFFICE (AUR)" pkg_install onlyoffice
