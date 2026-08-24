# session: wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/wayland.sh — Wayland stack + XWayland + Qt/GTK layer-shell libs that the
# Hyprland session builds on. ONE copy, POSIX (was .../modules/wayland.sh). Pure
# package install, no config. Names are Arch packages (rice is Arch-only);
# duplicates in the legacy list have been removed.
run_step "Installing Wayland stack" pkg_install \
    xorg-xwayland xorg-xlsclients qt6-base qt6-5compat qt5-wayland layer-shell-qt5 \
    qt6-wayland layer-shell-qt glfw-wayland gtk3 gtk-layer-shell gtk4 gtk4-layer-shell \
    meson wayland libxcb xcb-util-wm xcb-util-keysyms pango cairo libinput libglvnd \
    uwsm wayland-protocols wayland-utils wl-clipboard xdg-desktop-portal \
    xdg-desktop-portal-gtk xdg-desktop-portal-wlr xdg-utils wlr-protocols
