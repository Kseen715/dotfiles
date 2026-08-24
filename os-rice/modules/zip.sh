# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/zip.sh — zip + unzip archivers. ONE copy, POSIX (was .../modules/zip.sh).
run_step "Installing zip and unzip" pkg_install zip unzip
