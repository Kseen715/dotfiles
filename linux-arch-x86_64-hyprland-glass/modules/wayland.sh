info "Installing wayland..."
trace pacman -S --needed --noconfirm xorg-xwayland xorg-xlsclients qt5-wayland qt6-wayland glfw-wayland gtk3 gtk4 meson wayland libxcb xcb-util-wm xcb-util-keysyms pango cairo libinput libglvnd uwsm  wayland-protocols wayland-utils wl-clipboard xdg-desktop-portal xdg-desktop-portal-gtk xdg-desktop-portal-wlr xdg-utils wayland-protocols wayland-utils wlr-protocols
info "Installing wayland dotfiles..."
trace mkdir -p /usr/share/wayland-sessions