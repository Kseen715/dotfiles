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

echo "Checking if root..."
if [[ $EUID -ne 0 ]]; then
    if ! command -v sudo &>/dev/null; then
        error "This script must be run as root. Use 'su' to switch to root user or install sudo"
    else
        warning "Running with sudo..."
        exec sudo "$0" "$@"
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
    trace bash "$SCRIPT_DIR/build-paru.sh"
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

echo "Installing wayland..."
trace pacman -S --needed --noconfirm xorg-xwayland xorg-xlsclients qt5-wayland qt6-wayland glfw-wayland gtk3 gtk4 meson wayland libxcb xcb-util-wm xcb-util-keysyms pango cairo libinput libglvnd
echo "Installing wayland dotfiles..."
trace mkdir -p /usr/share/wayland-sessions
trace cp config/wayland/hyprland.desktop /usr/share/wayland-sessions
if [ "$VIRT" = "vmware" ]; then
    echo "Detected VMware, installing VMware specific dotfiles..."
    trace cp config/wayland/hyprland-vmware.desktop /usr/share/wayland-sessions
    trace cp config/wayland/start-hyprland-vmware.sh /usr/share/wayland-sessions
fi

echo "Installing hyprland..."
trace pacman -S --needed --noconfirm hyprland hyprshot
echo "Installing hyprland dotfiles..."
trace mkdir -p ~/.config/hypr
trace cp config/hypr/hyprland.conf ~/.config/hypr/

echo "Installing sddm..."
trace pacman -S --needed --noconfirm sddm
echo "Installing sddm dotfiles..."
trace mkdir -p /etc/sddm.conf.d
trace cp config/sddm/hyprland.main.conf /etc/sddm.conf.d/

echo "Installing hyprpaper..."
trace pacman -S --needed --noconfirm hyprpaper
echo "Installing hyprpaper dotfiles..."
trace mkdir -p ~/.config/hypr
trace cp config/hypr/hyprpaper.conf ~/.config/hypr/

echo "Installing hyprpicker..."
trace pacman -S --needed --noconfirm hyprpicker

echo "Installing waybar..."
trace pacman -S --needed --noconfirm waybar gsimplecal
trace echo "Installing waybar dotfiles..."
trace mkdir -p ~/.config/waybar
trace cp config/waybar/config.jsonc ~/.config/waybar/
trace cp config/waybar/style.css ~/.config/waybar/

echo "Installing zsh, dependencies and dotfiles..."
trace cd $DOTFILES_KSEEN715_REPO/zsh
trace chmod +x ./install-run.sh
trace ./install-run.sh -y
trace cd $SCRIPT_DIR

echo "Installing wofi..."
trace pacman -S --needed --noconfirm wofi
echo "Installing wofi dotfiles..."
trace mkdir -p ~/.config/wofi
trace cp config/wofi/config ~/.config/wofi/
trace cp config/wofi/style.css ~/.config/wofi/

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
    trace mkdir -p ~/.config/foot
    trace cp config/foot/foot.ini ~/.config/foot/
fi

success "Setup completed successfully!"
