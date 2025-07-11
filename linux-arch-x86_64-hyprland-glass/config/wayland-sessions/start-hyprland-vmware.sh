#!/usr/bin/env bash
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=Hyprland
export XDG_CURRENT_DESKTOP=Hyprland
export GDK_BACKEND="wayland,x11,*"
export ELECTRON_OZONE_PLATFORM_HINT=wayland
export WAYLAND_DISPLAY="wayland-0"
export DISPLAY="wayland-0"
export QT_QPA_PLATFORM="wayland;xcb"
export SDL_VIDEODRIVER="wayland"
export CLUTTER_BACKEND="wayland"
export STEAM_FORCE_DESKTOPUI_SCALING=1

export GSK_RENDERER=cairo
export WLR_NO_HARDWARE_CURSORS=1
export WLR_RENDERER_ALLOW_SOFTWARE=1
exec Hyprland