# modules/i3.sh — i3 window manager (X11) + dotfiles config. ONE copy, POSIX.
# config is dotfiles-owned (§5), overwritten on update. i3 is X11; the current
# rices are Wayland/Hyprland, so no rice ships an i3 theme — it is an available
# standalone module (`osr module i3`). pacman ships it as i3-wm (pacman.map).
run_step "Installing i3" pkg_install i3

if [ -f "$OSR_DOTFILES/i3/.config/i3/config" ]; then
    install_layer "$OSR_DOTFILES/i3/.config/i3/config" "$OSR_HOME/.config/i3/config"
fi
