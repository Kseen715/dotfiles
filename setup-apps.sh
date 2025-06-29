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
    trace bash "$SCRIPT_DIR/setup-paru.sh"
fi
echo "Using $AUR_HELPER as the AUR helper"

# echo "Downloading dotfiles from Kseen715..."
# DOTFILES_KSEEN715_REPO="$TMP_FOLDER/dotfiles_Kseen715"
# trace rm -rf $DOTFILES_KSEEN715_REPO
# trace git clone https://github.com/Kseen715/dotfiles $DOTFILES_KSEEN715_REPO --depth 1

echo "Installing micro..."
trace pacman -S --needed --noconfirm micro

echo "Installing Zen Browser..."
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm zen-browser-bin

echo "Installing Firefox..."
trace pacman -S --needed --noconfirm firefox

echo "Installing VSCode Insiders..."
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm visual-studio-code-insiders-bin

echo "Installing Discord..."
trace pacman -S --needed --noconfirm discord

echo "Installing Telegram..."
trace pacman -S --needed --noconfirm telegram-desktop

echo "Installing OBS Studio..."
trace pacman -S --needed --noconfirm obs-studio
trace mkdir -p /home/$DELEVATED_USER/.config/obs-studio
trace chown -R $DELEVATED_USER:$DELEVATED_USER /home/$DELEVATED_USER/.config/obs-studio

echo "Installing qbittorrent..."
trace pacman -S --needed --noconfirm qbittorrent
trace mkdir -p /home/$DELEVATED_USER/.config/qBittorrent
trace chown -R $DELEVATED_USER:$DELEVATED_USER /home/$DELEVATED_USER/.config/qBittorrent

# echo "Installing Flatpak..."
# trace pacman -S --needed --noconfirm flatpak
# trace flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo


echo "Installing Steam..."
# Detect AMD/NVIDIA/Intel GPU and install appropriate drivers
if lspci | grep -i "vga" | grep -i "nvidia" &>/dev/null; then
    IS_NVIDIA_GPU=true
    echo "NVIDIA GPU detected"
    trace pacman -S --needed --noconfirm nvidia nvidia-utils lib32-nvidia-utils vulkan-icd-loader lib32-vulkan-icd-loader nvidia-settings
fi
if lspci | grep -i "vga" | grep -i "amd" &>/dev/null; then
    IS_AMD_GPU=true
    echo "AMD GPU detected"
    trace pacman -S --needed --noconfirm mesa lib32-mesa vulkan-radeon lib32-vulkan-radeon
fi
if lspci | grep -i "vga" | grep -i "intel" &>/dev/null; then
    IS_INTEL_GPU=true
    echo "Intel GPU detected"
    trace pacman -S --needed --noconfirm mesa lib32-mesa vulkan-intel lib32-vulkan-intel
fi
if lspci | grep -i "vga" | grep -i "vmware" &>/dev/null; then
    IS_VMWARE_GPU=true
    echo "VMware GPU detected"
    trace pacman -S --needed --noconfirm open-vm-tools
    sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm xf86-video-vmware-git
fi
if [ -z "$IS_NVIDIA_GPU" ] && [ -z "$IS_AMD_GPU" ] && [ -z "$IS_INTEL_GPU" ] && [ -z "$IS_VMWARE_GPU" ]; then
    warning "No supported GPU detected"
fi
# we need to add STEAM_FORCE_DESKTOPUI_SCALING=1 steam to the environment variables somewhere for wayland support
if ! grep -q "STEAM_FORCE_DESKTOPUI_SCALING" /home/$DELEVATED_USER/.bashrc; then
    echo "Adding STEAM_FORCE_DESKTOPUI_SCALING=1 to /home/$DELEVATED_USER/.bashrc"
    echo "export STEAM_FORCE_DESKTOPUI_SCALING=1" >> /home/$DELEVATED_USER/.bashrc
else
    warning "STEAM_FORCE_DESKTOPUI_SCALING already exists in /home/$DELEVATED_USER/.bashrc"
fi
# add multilib repository to pacman.conf if not already present (can be commented out, if so - add it anyway)
if ! grep -q "^\[multilib\]" /etc/pacman.conf; then
    echo "Adding multilib repository to /etc/pacman.conf"
    trace tee -a /etc/pacman.conf <<EOF

[multilib]
Include = /etc/pacman.d/mirrorlist
EOF
else
    echo "Multilib repository already exists in /etc/pacman.conf"
fi
trace pacman -S --needed --noconfirm ttf-liberation vulkan-tools lib32-systemd
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm steam
# if using systemd-resolved, create a symlink for resolv.conf
if [ -d /run/systemd/resolve ]; then
    echo "Systemd-resolved detected, creating symlink for resolv.conf"
    trace ln -sf ../run/systemd/resolve/stub-resolv.conf /etc/resolv.conf
else
    echo "Systemd-resolved not detected, skipping resolv.conf symlink creation"
fi
# trace flatpak install flathub com.valvesoftware.Steam --assumeyes

success "Apps installed successfully"