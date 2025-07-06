#!/bin/bash

source "$(dirname "$(realpath "$0")")/src/common.sh"

# Update sources
info "Updating package sources..."
trace pacman -Sy --noconfirm 

source "$(dirname "$(realpath "$0")")/modules/git.sh"

source "$(dirname "$(realpath "$0")")/src/detect-aur-helper.sh"

source "$(dirname "$(realpath "$0")")/src/detect-virt.sh"

source "$(dirname "$(realpath "$0")")/modules/vmware-init.sh"

source "$(dirname "$(realpath "$0")")/src/detect-gpu.sh"

source "$(dirname "$(realpath "$0")")/apps/micro.sh"

source "$(dirname "$(realpath "$0")")/apps/htop.sh"

source "$(dirname "$(realpath "$0")")/apps/btop.sh"

source "$(dirname "$(realpath "$0")")/apps/fastfetch.sh"

source "$(dirname "$(realpath "$0")")/apps/zen-browser.sh"

source "$(dirname "$(realpath "$0")")/apps/firefox.sh"

source "$(dirname "$(realpath "$0")")/apps/vscode-insiders.sh"

source "$(dirname "$(realpath "$0")")/apps/discord.sh"

source "$(dirname "$(realpath "$0")")/apps/telegram.sh"

source "$(dirname "$(realpath "$0")")/apps/obs-studio.sh"

source "$(dirname "$(realpath "$0")")/apps/qbittorrent.sh"

source "$(dirname "$(realpath "$0")")/apps/steam.sh"

success "Apps installed successfully"