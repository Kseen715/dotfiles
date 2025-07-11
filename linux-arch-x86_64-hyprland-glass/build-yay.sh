#!/usr/bin/env bash
# ==============================================================================

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

# Folder for temp repos
TMP_FOLDER="/tmp/setup"
trace mkdir -p $TMP_FOLDER

# Deps for stuff
echo "Installing dependencies for setup..."
trace sudo pacman -S --needed --noconfirm base-devel git

# YAY
echo "Installing YAY..."
YAY_REPO="$TMP_FOLDER/yay"
trace rm -rf $YAY_REPO
trace git clone https://aur.archlinux.org/yay.git $YAY_REPO --depth 1 --branch master

# If yay is already installed, compare the version
if command -v yay &>/dev/null; then
    CURRENT_VERSION=$(pacman -Qi yay | grep Version | awk '{print $3}')
    LATEST_VERSION=$(yay -Si yay | grep Version | awk '{print $3}')
    echo "Current YAY version: $CURRENT_VERSION"
    echo "Latest YAY version: $LATEST_VERSION"
    
    if [ "$CURRENT_VERSION" = "$LATEST_VERSION" ]; then
        success "YAY is already up to date."
        exit 0
    else
        warning "Updating YAY from version $CURRENT_VERSION to $LATEST_VERSION..."
    fi
fi
trace makepkg -si --needed --noconfirm -D $YAY_REPO

# test if yay is installed
if ! command -v yay &>/dev/null; then
    error "YAY installation failed. Please check the logs."
fi
success "YAY installed successfully."