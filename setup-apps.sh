#!/usr/bin/env bash
# ==============================================================================

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

# ==============================================================================

TMP_FOLDER="/tmp/setup"
trace mkdir -p $TMP_FOLDER

# Update sources
echo "Updating package sources..."
trace sudo pacman -Sy --noconfirm 

# Ensure git is installed
if ! command -v git &>/dev/null; then
    echo "Git not found. Installing git..."
    trace sudo pacman -S --needed --noconfirm git
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
trace sudo pacman -S --needed --noconfirm micro

echo "Installing Zen Browser..."
trace sudo $AUR_HELPER -S --needed --noconfirm zen-browser-bin

echo "Installing Firefox..."
trace sudo pacman -S --needed --noconfirm firefox

echo "Installing VSCode Insiders..."
trace sudo $AUR_HELPER -S --needed --noconfirm visual-studio-code-insiders-bin

echo "Installing Discord..."
trace sudo pacman -S --needed --noconfirm discord

echo "Installing Telegram..."
trace sudo pacman -S --needed --noconfirm telegram-desktop

echo "Installing OBS Studio..."
trace sudo pacman -S --needed --noconfirm obs-studio

echo "Installing qbittorrent..."
trace sudo pacman -S --needed --noconfirm qbittorrent

# echo "Installing Flatpak..."
# trace sudo pacman -S --needed --noconfirm flatpak
# trace flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo


echo "Installing Steam..."
# Detect AMD/NVIDIA/Intel GPU and install appropriate drivers
if lspci | grep -i "vga" | grep -i "nvidia" &>/dev/null; then
    IS_NVIDIA_GPU=true
    echo "NVIDIA GPU detected"
    trace sudo pacman -S --needed --noconfirm nvidia nvidia-utils lib32-nvidia-utils vulkan-icd-loader lib32-vulkan-icd-loader nvidia-settings
fi
if lspci | grep -i "vga" | grep -i "amd" &>/dev/null; then
    IS_AMD_GPU=true
    echo "AMD GPU detected"
    trace sudo pacman -S --needed --noconfirm mesa lib32-mesa vulkan-radeon lib32-vulkan-radeon
fi
if lspci | grep -i "vga" | grep -i "intel" &>/dev/null; then
    IS_INTEL_GPU=true
    echo "Intel GPU detected"
    trace sudo pacman -S --needed --noconfirm mesa lib32-mesa vulkan-intel lib32-vulkan-intel
fi
if lspci | grep -i "vga" | grep -i "vmware" &>/dev/null; then
    IS_VMWARE_GPU=true
    echo "VMware GPU detected"
    trace sudo pacman -S --needed --noconfirm open-vm-tools lib32-vulkan-virtio
fi
if [ -z "$IS_NVIDIA_GPU" ] && [ -z "$IS_AMD_GPU" ] && [ -z "$IS_INTEL_GPU" ] && [ -z "$IS_VMWARE_GPU" ]; then
    warning "No supported GPU detected"
fi
# we need to add STEAM_FORCE_DESKTOPUI_SCALING=1 steam to the environment variables somewhere for wayland support
if ! grep -q "STEAM_FORCE_DESKTOPUI_SCALING" ~/.bashrc; then
    echo "Adding STEAM_FORCE_DESKTOPUI_SCALING=1 to ~/.bashrc"
    echo "export STEAM_FORCE_DESKTOPUI_SCALING=1" >> ~/.bashrc
else
    warning "STEAM_FORCE_DESKTOPUI_SCALING already exists in ~/.bashrc"
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
trace sudo pacman -S --needed --noconfirm ttf-liberation vulkan-tools lib32-systemd
trace sudo paru -S --needed --noconfirm steam
# if using systemd-resolved, create a symlink for resolv.conf
if [ -d /run/systemd/resolve ]; then
    echo "Systemd-resolved detected, creating symlink for resolv.conf"
    trace sudo ln -sf ../run/systemd/resolve/stub-resolv.conf /mnt/etc/resolv.conf
else
    echo "Systemd-resolved not detected, skipping resolv.conf symlink creation"
fi
# trace flatpak install flathub com.valvesoftware.Steam --assumeyes

