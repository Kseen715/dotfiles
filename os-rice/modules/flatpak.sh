# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/flatpak.sh — Flatpak + the Flathub remote. ONE copy, POSIX
# (was .../apps/flatpack.sh). Adding the remote is idempotent (--if-not-exists).
run_step "Installing Flatpak" pkg_install flatpak

# The remote must be added AS ROOT with an explicit --system: `flatpak
# remote-add` defaults to the system installation, and a non-root user touching
# it goes through polkit - which has no agent (and no session) under the
# installer's sudo -u, so it dies with "operation EnsureRepo not allowed for
# user". Root writes /var/lib/flatpak directly, no polkit involved, and every
# user on the box gets Flathub.
run_step "Adding Flathub remote" as_root flatpak remote-add --system \
    --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
