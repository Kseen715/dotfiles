# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/flatpak.sh — Flatpak + the Flathub remote. ONE copy, POSIX
# (was .../apps/flatpack.sh). Adding the remote is idempotent (--if-not-exists).
run_step "Installing Flatpak" pkg_install flatpak
run_step "Adding Flathub remote" as_user flatpak remote-add --if-not-exists \
    flathub https://flathub.org/repo/flathub.flatpakrepo
