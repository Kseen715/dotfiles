#!/bin/bash

source "$(dirname "$(realpath "$0")")/common.sh"

TMP_FOLDER="/tmp/setup"
trace mkdir -p $TMP_FOLDER

# Update sources
info "Updating package sources..."
trace pacman -Sy --noconfirm 

# Ensure git is installed
if ! command -v git &>/dev/null; then
    info "Git not found. Installing git..."
    trace pacman -S --needed --noconfirm git
fi

# Check if yay and/or paru is installed and chose one as preferred AUR helper.
# (paru is preferred if both are installed)
if command -v yay &>/dev/null; then
    info "YAY detected"
    AUR_HELPER="yay"
fi
if command -v paru &>/dev/null; then
    info "PARU detected"
    AUR_HELPER="paru"
fi
if [ -z "$AUR_HELPER" ]; then
    warning "No AUR helper found. Installing PARU as the default AUR helper"
    # Run setup-paru.sh script to install paru
    # run as non-root user to avoid permission issues
    trace bash "$SCRIPT_DIR/build-paru.sh" --delevated "$DELEVATED_USER"
fi
info "Using $AUR_HELPER as the AUR helper"

VIRT=""
# Detect virtualizations (VMware, VirtualBox, QEMU, etc.)
if command -v lscpu &>/dev/null; then
    if lscpu | grep -q "VMware"; then
        info "VMware detected"
        VIRT="vmware"
    elif lscpu | grep -q "VirtualBox"; then
        info "VirtualBox detected"
        VIRT="virtualbox"
    elif lscpu | grep -q "QEMU"; then
        info "QEMU detected"
        VIRT="qemu"
    fi
fi

info "Installing minimal text editors..."
trace pacman -S --needed --noconfirm nano vim

info "Installing wget..."
trace pacman -S --needed --noconfirm wget

info "Downloading dotfiles from Kseen715..."
DOTFILES_KSEEN715_REPO="$TMP_FOLDER/dotfiles_Kseen715"
trace rm -rf $DOTFILES_KSEEN715_REPO
trace git clone https://github.com/Kseen715/dotfiles $DOTFILES_KSEEN715_REPO --depth 1

info "Installing video drivers..."
# Install video drivers based on virtualization type and hardware if not virtualized
if [ "$VIRT" = "vmware" ]; then
    info "Detected VMware, installing VMware specific GPU drivers..."
    trace pacman -S --needed --noconfirm open-vm-tools mesa
    sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm xf86-video-vmware-git
    info "Activating VMware tools..."
    trace systemctl enable vmtoolsd.service --force
    trace systemctl enable vmware-vmblock-fuse.service --force
fi

GPU_VENDOR=""
# Detect GPU vendor, including virtualized environments
if command -v lspci &>/dev/null; then
    GPU_VENDOR=$(lspci | grep -E "VGA|3D" | awk -F: '{print $3}' | awk '{print $1}')
    if [[ "$GPU_VENDOR" == "NVIDIA" ]]; then
        info "NVIDIA GPU detected"
    elif [[ "$GPU_VENDOR" == "AMD" ]]; then
        info "AMD GPU detected"
    elif [[ "$GPU_VENDOR" == "Intel" ]]; then
        info "Intel GPU detected"
    elif [[ "$GPU_VENDOR" == "VMware" ]]; then
        info "VMware GPU detected"
    elif [[ "$GPU_VENDOR" == "VirtualBox" ]]; then
        info "VirtualBox GPU detected"
    elif [[ -z "$GPU_VENDOR" ]]; then
        info "No GPU detected, assuming virtualized environment with no dedicated GPU"
    else
        warning "Unknown GPU vendor: $GPU_VENDOR"
    fi
else
    warning "lspci command not found, unable to detect GPU vendor"
fi

# add multilib repository to pacman.conf if not already present (can be commented out, if so - add it anyway)
if ! grep -q "^\[multilib\]" /etc/pacman.conf; then
    info "Adding multilib repository to /etc/pacman.conf"
    trace sudo tee -a /etc/pacman.conf <<EOF

[multilib]
Include = /etc/pacman.d/mirrorlist
EOF
else
    info "Multilib repository already exists in /etc/pacman.conf"
fi
trace pacman -Sy

info "Installing GPU drivers..."
if [ "$GPU_VENDOR" == "NVIDIA" ]; then
    info "NVIDIA GPU detected"
    trace sudo pacman -S --needed --noconfirm nvidia nvidia-utils lib32-nvidia-utils vulkan-icd-loader lib32-vulkan-icd-loader nvidia-settings
fi
if [ "$GPU_VENDOR" == "AMD" ]; then
    info "AMD GPU detected"
    trace sudo pacman -S --needed --noconfirm mesa lib32-mesa vulkan-radeon lib32-vulkan-radeon
fi
if [ "$GPU_VENDOR" == "Intel" ]; then
    info "Intel GPU detected"
    trace sudo pacman -S --needed --noconfirm mesa lib32-mesa vulkan-intel lib32-vulkan-intel
fi
if [ "$GPU_VENDOR" == "VMware" ]; then
    info "VMware GPU detected"
    trace sudo pacman -S --needed --noconfirm open-vm-tools mesa lib32-vulkan-virtio
fi

info "Installing wayland..."
trace pacman -S --needed --noconfirm xorg-xwayland xorg-xlsclients qt5-wayland qt6-wayland glfw-wayland gtk3 gtk4 meson wayland libxcb xcb-util-wm xcb-util-keysyms pango cairo libinput libglvnd uwsm  wayland-protocols wayland-utils wl-clipboard xdg-desktop-portal xdg-desktop-portal-gtk xdg-desktop-portal-wlr xdg-utils wayland-protocols wayland-utils wlr-protocols
info "Installing wayland dotfiles..."
trace mkdir -p /usr/share/wayland-sessions

info "Installing hyprland..."
# check if hyprland is not already installed
if ! command -v hyprctl &>/dev/null; then
    trace rm /usr/share/wayland-sessions/hyprland.desktop
fi
trace pacman -S --needed --noconfirm hyprland hyprshot xdg-desktop-portal-hyprland
info "Installing hyprland dotfiles..."
sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config
sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config/hypr

trace cp config/hypr/hyprland.conf /home/$DELEVATED_USER/.config/hypr/

trace cp config/hypr/start-easyeffects.sh /home/$DELEVATED_USER/.config/hypr/start-easyeffects.sh
trace chmod +x /home/$DELEVATED_USER/.config/hypr/start-easyeffects.sh
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/hypr/start-easyeffects.sh

trace cp config/hypr/start-cliphist-store.sh /home/$DELEVATED_USER/.config/hypr/start-cliphist-store.sh
trace chmod +x /home/$DELEVATED_USER/.config/hypr/start-cliphist-store.sh
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/hypr/start-cliphist-store.sh
info "Checking if Golang installed..."
if ! command -v go &>/dev/null; then
    info "Golang not found. Installing Golang..."
    trace pacman -S --needed --noconfirm go
else
    info "Golang is installed"
fi
trace go install github.com/pdf/cliphist-wofi-img@latest
trace wget https://raw.githubusercontent.com/sentriz/cliphist/refs/heads/master/contrib/cliphist-wofi-img -O /usr/local/bin/cliphist-wofi-img
trace chmod +x /usr/local/bin/cliphist-wofi-img
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /usr/local/bin/cliphist-wofi-img

trace cp config/wayland-sessions/hyprland.desktop /usr/share/wayland-sessions/hyprland.desktop
if [ "$VIRT" = "vmware" ]; then
    trace cp config/wayland-sessions/hyprland-vmware.desktop /usr/share/wayland-sessions/hyprland-vmware.desktop
    trace cp config/wayland-sessions/start-hyprland-vmware.sh /usr/share/wayland-sessions/start-hyprland-vmware.sh
    trace chmod +x /usr/share/wayland-sessions/start-hyprland-vmware.sh
    # Give execute permissions to the delevated user, so sddm can run it
    trace chown "$DELEVATED_USER":"$DELEVATED_USER" /usr/share/wayland-sessions/start-hyprland-vmware.sh
fi

info "Installing sddm..."
trace pacman -S --needed --noconfirm sddm qt6-5compat qt6-declarative qt6-svg
info "Installing sddm dotfiles..."
trace mkdir -p /etc/sddm.conf.d
trace cp config/sddm/hyprland.main.conf /etc/sddm.conf.d/sddm.conf
info "Installing sddm theme..." # config/sddm/jakoolit-theme
trace mkdir -p /usr/share/sddm/themes/jakoolit-theme
trace cp -r config/sddm/jakoolit-theme /usr/share/sddm/themes
trace mkdir -p /etc/sddm.conf.d
trace cp config/sddm/theme.conf.user /etc/sddm.conf.d/theme.conf.user
info "Activating sddm..."
trace systemctl enable sddm.service --force 

info "Installing hyprpaper..."
trace pacman -S --needed --noconfirm hyprpaper
info "Installing hyprpaper dotfiles..."
sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config/hypr
trace cp config/hypr/hyprpaper.conf /home/$DELEVATED_USER/.config/hypr/
mkdir -p /home/$DELEVATED_USER/Pictures/Wallpapers
chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/Pictures/Wallpapers
trace cp wallpapers/* /home/$DELEVATED_USER/Pictures/Wallpapers/

info "Installing hyprpicker..."
trace pacman -S --needed --noconfirm hyprpicker

info "Installing waybar..."
trace pacman -S --needed --noconfirm waybar gsimplecal
trace info "Installing waybar dotfiles..."
sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config/waybar
trace cp config/waybar/config.jsonc /home/$DELEVATED_USER/.config/waybar/
trace cp config/waybar/style.css /home/$DELEVATED_USER/.config/waybar/

info "Installing zsh, dependencies and dotfiles..."
trace cd $DOTFILES_KSEEN715_REPO/zsh
trace chmod +x ./install-run.sh
sudo -u "$DELEVATED_USER" ./install-run.sh -y
trace cd $SCRIPT_DIR

info "Installing helvum, easyeffects..."
trace pacman -S --needed --noconfirm helvum easyeffects
sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config/dconf
sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config/easyeffects/
info "Installing easyeffects plugins..."
trace pacman -S --needed --noconfirm lsp-plugins lsp-plugins-ladspa calf libebur128 zam-plugins zita-convolver speex soundtouch rnnoise libsamplerate libsndfile libbs2b fftw speexdsp nlohmann-json onetbb
sudo -u "$DELEVATED_USER" paru -S --needed --noconfirm mda-lv2-git libdeep_filter_ladspa-bin

info "Installing wofi..."
trace pacman -S --needed --noconfirm wofi
info "Installing wofi dotfiles..."
sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config/wofi
trace cp config/wofi/config /home/$DELEVATED_USER/.config/wofi/
trace cp config/wofi/style.css /home/$DELEVATED_USER/.config/wofi/

info "Installing wezterm..."
trace pacman -S --needed --noconfirm wezterm
info "Installing dotfiles for wezterm..."
trace cd $DOTFILES_KSEEN715_REPO/wezterm
trace pacman -S --needed --noconfirm ttf-jetbrains-mono-nerd noto-fonts-emoji
trace chmod +x ./install.sh
trace ./install.sh -y
trace cd $SCRIPT_DIR

info "Installing foot..."
trace pacman -S --needed --noconfirm foot
info "Installing foot dotfiles..."
sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config/foot
trace cp config/foot/foot.ini /home/$DELEVATED_USER/.config/foot/

# cliphist
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
