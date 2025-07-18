#!/bin/bash

source "$(dirname "$(realpath "$0")")/src/common.sh"

# Update sources
info "Updating package sources..."
trace pacman -Sy --noconfirm 

source "$(dirname "$(realpath "$0")")/modules/git.sh"

source "$(dirname "$(realpath "$0")")/src/detect-aur-helper.sh"
source "$(dirname "$(realpath "$0")")/src/detect-virt.sh"
source "$(dirname "$(realpath "$0")")/src/detect-cpu.sh"
source "$(dirname "$(realpath "$0")")/modules/vmware-init.sh"
source "$(dirname "$(realpath "$0")")/src/detect-gpu.sh"

source "$(dirname "$(realpath "$0")")/modules/pacman-multilib.sh"
source "$(dirname "$(realpath "$0")")/modules/cpu-microcodes.sh"
source "$(dirname "$(realpath "$0")")/modules/gpu-drivers.sh"

source "$(dirname "$(realpath "$0")")/src/repo-dotfiles.sh"

source "$(dirname "$(realpath "$0")")/modules/wayland.sh"

source "$(dirname "$(realpath "$0")")/modules/hyprland.sh"
source "$(dirname "$(realpath "$0")")/modules/sddm.sh"
source "$(dirname "$(realpath "$0")")/modules/hyprpaper.sh"
source "$(dirname "$(realpath "$0")")/modules/hyprpicker.sh"
source "$(dirname "$(realpath "$0")")/modules/hyprlock.sh"
source "$(dirname "$(realpath "$0")")/modules/hyprcursor.sh"
source "$(dirname "$(realpath "$0")")/modules/hypridle.sh"
source "$(dirname "$(realpath "$0")")/modules/waybar.sh"
source "$(dirname "$(realpath "$0")")/modules/wleave.sh"
source "$(dirname "$(realpath "$0")")/modules/mako.sh"
source "$(dirname "$(realpath "$0")")/modules/gtklock.sh"
source "$(dirname "$(realpath "$0")")/modules/wofi.sh"
source "$(dirname "$(realpath "$0")")/modules/cliphist.sh"

source "$(dirname "$(realpath "$0")")/modules/zsh.sh"

source "$(dirname "$(realpath "$0")")/modules/qpwgraph.sh"
source "$(dirname "$(realpath "$0")")/modules/easyeffects.sh"

source "$(dirname "$(realpath "$0")")/modules/luminance.sh"
source "$(dirname "$(realpath "$0")")/modules/nwg-displays.sh"
source "$(dirname "$(realpath "$0")")/modules/printer.sh"

source "$(dirname "$(realpath "$0")")/modules/wezterm.sh"
source "$(dirname "$(realpath "$0")")/modules/foot.sh"
source "$(dirname "$(realpath "$0")")/modules/nautilus.sh"
source "$(dirname "$(realpath "$0")")/modules/kate.sh"

# blue=(
#   bluez
#   bluez-utils
#   blueman
# )

# from gnome ???
# xdg-user-dirs-gtk

success "Setup completed successfully!"
