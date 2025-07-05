#!/bin/bash

source "$(dirname "$(realpath "$0")")/src/common.sh"

# Update sources
info "Updating package sources..."
trace pacman -Sy --noconfirm 

source "$(dirname "$(realpath "$0")")/modules/git.sh"

# Detect AUR helper & install if not found
source "$(dirname "$(realpath "$0")")/src/detect-aur-helper.sh"

# Detect virtualization type
source "$(dirname "$(realpath "$0")")/src/detect-virt.sh"

info "Downloading dotfiles from Kseen715..."
DOTFILES_KSEEN715_REPO="$TMP_FOLDER/dotfiles_Kseen715"
trace rm -rf $DOTFILES_KSEEN715_REPO
trace git clone https://github.com/Kseen715/dotfiles $DOTFILES_KSEEN715_REPO --depth 1

source "$(dirname "$(realpath "$0")")/vmware-init.sh"

# Detect GPU vendor, including virtualized environments
source "$(dirname "$(realpath "$0")")/src/detect-gpu.sh"

source "$(dirname "$(realpath "$0")")/modules/pacman-multilib.sh"

source "$(dirname "$(realpath "$0")")/modules/gpu-drivers.sh"

source "$(dirname "$(realpath "$0")")/modules/wayland.sh"

source "$(dirname "$(realpath "$0")")/modules/hyprland.sh"

source "$(dirname "$(realpath "$0")")/modules/sddm.sh"

source "$(dirname "$(realpath "$0")")/modules/hyprpaper.sh"

source "$(dirname "$(realpath "$0")")/modules/hyprpicker.sh"

source "$(dirname "$(realpath "$0")")/modules/waybar.sh"

source "$(dirname "$(realpath "$0")")/modules/zsh.sh"

source "$(dirname "$(realpath "$0")")/modules/helvum.sh"

source "$(dirname "$(realpath "$0")")/modules/easyeffects.sh"

source "$(dirname "$(realpath "$0")")/modules/wofi.sh"

source "$(dirname "$(realpath "$0")")/modules/wezterm.sh"

source "$(dirname "$(realpath "$0")")/modules/foot.sh"

#  qt5ct
#   qt6ct
#   qt6-svg
#   wl-clipboard
#   wlogout
#   xdg-user-dirs
#   xdg-utils 
# blue=(
#   bluez
#   bluez-utils
#   blueman
# )

# from gnome ???
# xdg-user-dirs-gtk

success "Setup completed successfully!"
