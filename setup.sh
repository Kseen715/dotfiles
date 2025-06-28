#!/bin/bash
# ==============================================================================

# Grab --delevated <username> argument if provided
if [[ "$1" == "--delevated" && -n "$2" ]]; then
    DELEVATED_USER="$2"
    shift 2 # Remove the first two arguments
else
    DELEVATED_USER=""
fi

# Signal handler for Ctrl+C
cleanup() {
    echo ""
    error "Script interrupted by user (Ctrl+C). Exiting..."
}

# Trap SIGINT (Ctrl+C) and call cleanup function
trap cleanup SIGINT SIGTERM SIGQUIT

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Enhanced logging functions with colors
echo() {
    printf "${CYAN}[INFO]${NC}\t%s\n" "$*"
}

warning() {
    printf "${YELLOW}[WARN]${NC}\t%s\n" "$*" >&2
}

error() {
    printf "${RED}[ERROR]${NC}\t%s\n" "$*" >&2
    exit 1
}

success() {
    printf "${GREEN}[DONE]${NC}\t%s\n" "$*"
}

trace() {
    printf "${NC}[BASH]${NC}\t%s\n" "$*"
    "$@"
}

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)

echo "Checking if root..."
if [[ $EUID -ne 0 ]]; then
    # If not root, save username and re-execute with sudo
    USERNAME=$(whoami)
    echo "Current user: $USERNAME"
    if ! command -v sudo &>/dev/null; then
        error "This script must be run as root. Use 'su' to switch to root user or install sudo"
    else
        warning "Running with sudo..."
        # use absolute path to the script to avoid issues with relative paths
        if [[ ! -f "$SCRIPT_DIR/$(basename "$0")" ]]; then
            error "Script not found at expected location: $SCRIPT_DIR/$(basename "$0")"
        fi
        # Re-executes the script with sudo
        trace chmod +x "$SCRIPT_DIR/$(basename "$0")"
        exec sudo "$SCRIPT_DIR/$(basename "$0")" --delevated "$USERNAME" "$@"
        exit 0
    fi
fi

# ==============================================================================

TMP_FOLDER="/tmp/setup"
trace mkdir -p $TMP_FOLDER

# Update sources
echo "Updating package sources..."
trace pacman -Sy --noconfirm 

# Ensure git is installed
if ! command -v git &>/dev/null; then
    echo "Git not found. Installing git..."
    trace pacman -S --needed --noconfirm git
fi

# Check if yay and/or paru is installed and chose one as preferred AUR helper.
# (paru is preferred if both are installed)
if command -v yay &>/dev/null; then
    echo "YAY detected"
    AUR_HELPER="yay"
fi
if command -v paru &>/dev/null; then
    echo "PARU detected"
    AUR_HELPER="paru"
fi
if [ -z "$AUR_HELPER" ]; then
    warning "No AUR helper found. Installing PARU as the default AUR helper"
    # Run setup-paru.sh script to install paru
    # run as non-root user to avoid permission issues
    trace bash "$SCRIPT_DIR/build-paru.sh" --delevated "$DELEVATED_USER"
fi
echo "Using $AUR_HELPER as the AUR helper"

VIRT=""
# Detect virtualizations (VMware, VirtualBox, QEMU, etc.)
if command -v lscpu &>/dev/null; then
    if lscpu | grep -q "VMware"; then
        echo "VMware detected"
        VIRT="vmware"
    elif lscpu | grep -q "VirtualBox"; then
        echo "VirtualBox detected"
        VIRT="virtualbox"
    elif lscpu | grep -q "QEMU"; then
        echo "QEMU detected"
        VIRT="qemu"
    fi
fi

# Install minimal text editors 
echo "Installing minimal text editors..."
trace pacman -S --needed --noconfirm nano vim

echo "Downloading dotfiles from Kseen715..."
DOTFILES_KSEEN715_REPO="$TMP_FOLDER/dotfiles_Kseen715"
trace rm -rf $DOTFILES_KSEEN715_REPO
trace git clone https://github.com/Kseen715/dotfiles $DOTFILES_KSEEN715_REPO --depth 1

echo "Installing video drivers..."
# Install video drivers based on virtualization type and hardware if not virtualized
if [ "$VIRT" = "vmware" ]; then
    echo "Detected VMware, installing VMware specific GPU drivers..."
    trace pacman -S --needed --noconfirm open-vm-tools mesa
    sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm xf86-video-vmware
    echo "Activating VMware tools..."
    trace systemctl enable vmtoolsd.service --force
    trace systemctl enable vmware-vmblock-fuse.service --force
fi

GPU_VENDOR=""
# Detect GPU vendor, including virtualized environments
if command -v lspci &>/dev/null; then
    GPU_VENDOR=$(lspci | grep -E "VGA|3D" | awk -F: '{print $3}' | awk '{print $1}')
    if [[ "$GPU_VENDOR" == "NVIDIA" ]]; then
        echo "NVIDIA GPU detected"
    elif [[ "$GPU_VENDOR" == "AMD" ]]; then
        echo "AMD GPU detected"
    elif [[ "$GPU_VENDOR" == "Intel" ]]; then
        echo "Intel GPU detected"
    elif [[ "$GPU_VENDOR" == "VMware" ]]; then
        echo "VMware GPU detected"
    elif [[ "$GPU_VENDOR" == "VirtualBox" ]]; then
        echo "VirtualBox GPU detected"
    elif [[ -z "$GPU_VENDOR" ]]; then
        echo "No GPU detected, assuming virtualized environment with no dedicated GPU"
    else
        warning "Unknown GPU vendor: $GPU_VENDOR"
    fi
else
    warning "lspci command not found, unable to detect GPU vendor"
fi

# add multilib repository to pacman.conf if not already present (can be commented out, if so - add it anyway)
if ! grep -q "^\[multilib\]" /etc/pacman.conf; then
    echo "Adding multilib repository to /etc/pacman.conf"
    trace sudo tee -a /etc/pacman.conf <<EOF

[multilib]
Include = /etc/pacman.d/mirrorlist
EOF
else
    echo "Multilib repository already exists in /etc/pacman.conf"
fi
trace pacman -Sy

echo "Installing GPU drivers..."
if [ "$GPU_VENDOR" == "NVIDIA" ]; then
    echo "NVIDIA GPU detected"
    trace sudo pacman -S --needed --noconfirm nvidia nvidia-utils lib32-nvidia-utils vulkan-icd-loader lib32-vulkan-icd-loader nvidia-settings
fi
if [ "$GPU_VENDOR" == "AMD" ]; then
    echo "AMD GPU detected"
    trace sudo pacman -S --needed --noconfirm mesa lib32-mesa vulkan-radeon lib32-vulkan-radeon
fi
if [ "$GPU_VENDOR" == "Intel" ]; then
    echo "Intel GPU detected"
    trace sudo pacman -S --needed --noconfirm mesa lib32-mesa vulkan-intel lib32-vulkan-intel
fi
if [ "$GPU_VENDOR" == "VMware" ]; then
    echo "VMware GPU detected"
    trace sudo pacman -S --needed --noconfirm open-vm-tools mesa lib32-vulkan-virtio
fi

echo "Installing wayland..."
trace pacman -S --needed --noconfirm xorg-xwayland xorg-xlsclients qt5-wayland qt6-wayland glfw-wayland gtk3 gtk4 meson wayland libxcb xcb-util-wm xcb-util-keysyms pango cairo libinput libglvnd uwsm
echo "Installing wayland dotfiles..."
trace mkdir -p /usr/share/wayland-sessions

echo "Installing hyprland..."
# check if hyprland is not already installed
if ! command -v hyprctl &>/dev/null; then
    trace rm /usr/share/wayland-sessions/hyprland.desktop
fi
trace pacman -S --needed --noconfirm hyprland hyprshot
echo "Installing hyprland dotfiles..."
trace mkdir -p /home/$DELEVATED_USER/.config
trace mkdir -p /home/$DELEVATED_USER/.config/hypr
trace cp config/hypr/hyprland.conf /home/$DELEVATED_USER/.config/hypr/
trace cp config/wayland-sessions/hyprland.desktop /usr/share/wayland-sessions/hyprland.desktop
if [ "$VIRT" = "vmware" ]; then
    trace cp config/wayland-sessions/hyprland-vmware.desktop /usr/share/wayland-sessions/hyprland-vmware.desktop
    trace cp config/wayland-sessions/start-hyprland-vmware.sh /usr/share/wayland-sessions/start-hyprland-vmware.sh
    trace chmod +x /usr/share/wayland-sessions/start-hyprland-vmware.sh
    # Give execute permissions to the delevated user, so sddm can run it
    trace chown "$DELEVATED_USER":"$DELEVATED_USER" /usr/share/wayland-sessions/start-hyprland-vmware.sh
fi

echo "Installing sddm..."
trace pacman -S --needed --noconfirm sddm qt6-5compat qt6-declarative qt6-svg
echo "Installing sddm dotfiles..."
trace mkdir -p /etc/sddm.conf.d
trace cp config/sddm/hyprland.main.conf /etc/sddm.conf.d/sddm.conf
echo "Activating sddm..."
trace systemctl enable sddm.service --force 

echo "Installing hyprpaper..."
trace pacman -S --needed --noconfirm hyprpaper
echo "Installing hyprpaper dotfiles..."
trace mkdir -p /home/$DELEVATED_USER/.config/hypr
trace cp config/hypr/hyprpaper.conf /home/$DELEVATED_USER/.config/hypr/

echo "Installing hyprpicker..."
trace pacman -S --needed --noconfirm hyprpicker

echo "Installing waybar..."
trace pacman -S --needed --noconfirm waybar gsimplecal
trace echo "Installing waybar dotfiles..."
trace mkdir -p /home/$DELEVATED_USER/.config/waybar
trace cp config/waybar/config.jsonc /home/$DELEVATED_USER/.config/waybar/
trace cp config/waybar/style.css /home/$DELEVATED_USER/.config/waybar/

echo "Installing zsh, dependencies and dotfiles..."
trace cd $DOTFILES_KSEEN715_REPO/zsh
trace chmod +x ./install-run.sh
sudo -u "$DELEVATED_USER" ./install-run.sh -y
trace cd $SCRIPT_DIR

echo "Installing wofi..."
trace pacman -S --needed --noconfirm wofi
echo "Installing wofi dotfiles..."
trace mkdir -p /home/$DELEVATED_USER/.config/wofi
trace cp config/wofi/config /home/$DELEVATED_USER/.config/wofi/
trace cp config/wofi/style.css /home/$DELEVATED_USER/.config/wofi/

echo "Installing wezterm..."
trace pacman -S --needed --noconfirm wezterm
echo "Installing dotfiles for wezterm..."
trace cd $DOTFILES_KSEEN715_REPO/wezterm
trace pacman -S --needed --noconfirm ttf-jetbrains-mono-nerd noto-fonts-emoji
trace chmod +x ./install.sh
trace ./install.sh -y
trace cd $SCRIPT_DIR

if [ "$VIRT" = "vmware" ]; then
    echo "Installing foot..."
    trace pacman -S --needed --noconfirm foot
    echo "Installing foot dotfiles..."
    trace mkdir -p /home/$DELEVATED_USER/.config/foot
    trace cp config/foot/foot.ini /home/$DELEVATED_USER/.config/foot/
fi

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


success "Setup completed successfully!"
