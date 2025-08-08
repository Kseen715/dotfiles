#!/bin/bash

source "$(dirname "$(realpath "$0")")/src/common.sh"

# Update sources
info "Updating package sources..."
trace pacman -Sy --noconfirm 

source "$(dirname "$(realpath "$0")")/modules/git.sh"
source "$(dirname "$(realpath "$0")")/modules/openssh.sh"

source "$(dirname "$(realpath "$0")")/src/detect-aur-helper.sh"
source "$(dirname "$(realpath "$0")")/src/detect-virt.sh"
source "$(dirname "$(realpath "$0")")/src/detect-cpu.sh"
source "$(dirname "$(realpath "$0")")/modules/vmware-init.sh"
source "$(dirname "$(realpath "$0")")/src/detect-gpu.sh"

source "$(dirname "$(realpath "$0")")/modules/pacman-multilib.sh"
source "$(dirname "$(realpath "$0")")/modules/dkms.sh"
source "$(dirname "$(realpath "$0")")/modules/cpu-microcodes.sh"
source "$(dirname "$(realpath "$0")")/modules/gpu-drivers.sh"

source "$(dirname "$(realpath "$0")")/src/repo-dotfiles.sh"

source "$(dirname "$(realpath "$0")")/modules/zsh.sh"

success "Setup completed successfully!"
