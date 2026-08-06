# session: x11+wayland
# modules/xdg.sh — the XDG layer: portals, user dirs, MIME, desktop entries
# (i3-sugg §3.2 + §3.4). This is the module that decides whether "apps just
# work" or fail in ways nobody connects back to the WM:
#
#   no portal            Flatpak/Chromium/Electron file dialogs are blank
#   no XDG_CURRENT_DESKTOP  the portal cannot pick a backend at all
#   no xdg-user-dirs     browsers have no ~/Downloads to save into
#   no shared-mime-info  every file is application/octet-stream
#   no desktop-file-utils  "Open With" is empty
#
# i3 ships no portal backend of its own, so the gtk one is pinned explicitly.
# XDG_CURRENT_DESKTOP=i3 is exported by the xprofile layer (modules/xorg.sh).

run_step "Installing XDG portals + basics" pkg_install \
    xdg-desktop-portal xdg-desktop-portal-gtk \
    xdg-utils xdg-user-dirs xdg-user-dirs-gtk \
    shared-mime-info desktop-file-utils hicolor-icon-theme

# Pin the backend: i3 is not a desktop the portal knows, so without this it
# either picks nothing or picks whatever happens to be installed.
as_user mkdir -p "$OSR_HOME/.config/xdg-desktop-portal"
as_user tee "$OSR_HOME/.config/xdg-desktop-portal/i3-portals.conf" >/dev/null <<'EOF'
# Managed by os-rice (modules/xdg.sh) — i3 has no portal backend, so pin gtk.
[preferred]
default=gtk
org.freedesktop.impl.portal.Settings=gtk
EOF

# Create ~/Downloads, ~/Pictures, ... once. Idempotent by design; the databases
# below are best-effort (a fresh container has nothing to index).
run_step "Creating XDG user dirs" as_user xdg-user-dirs-update || :
command -v update-mime-database >/dev/null 2>&1 \
    && as_root update-mime-database /usr/share/mime >/dev/null 2>&1 || :
command -v update-desktop-database >/dev/null 2>&1 \
    && as_root update-desktop-database /usr/share/applications >/dev/null 2>&1 || :

# Default applications. Seeded once — which browser opens a link is the user's
# call, and rewriting it on every rice switch would be obnoxious.
if [ -f "$OSR_DOTFILES/xdg/mimeapps.list" ]; then
    seed_once "$OSR_DOTFILES/xdg/mimeapps.list" "$OSR_HOME/.config/mimeapps.list"
fi
