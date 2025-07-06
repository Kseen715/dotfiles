info "Installing wayland..."
trace pacman -S --needed --noconfirm xorg-xwayland xorg-xlsclients qt6-base qt6-5compat qt5-wayland layer-shell-qt5 qt6-wayland layer-shell-qt glfw-wayland gtk3 gtk-layer-shell gtk4 gtk4-layer-shell meson wayland libxcb xcb-util-wm xcb-util-keysyms pango cairo libinput libglvnd uwsm  wayland-protocols wayland-utils wl-clipboard xdg-desktop-portal xdg-desktop-portal-gtk xdg-desktop-portal-wlr xdg-utils wayland-protocols wayland-utils wlr-protocols 	libxcb
info "Installing wayland dotfiles..."
trace mkdir -p /usr/share/wayland-sessions