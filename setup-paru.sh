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

# PARU
echo "Installing PARU..."
PARU_REPO="$TMP_FOLDER/paru"
trace rm -rf $PARU_REPO
trace git clone https://aur.archlinux.org/paru.git $PARU_REPO --depth 1 --branch master

# If paru is already installed, compare the version
if command -v paru &>/dev/null; then
    CURRENT_VERSION=$(pacman -Qi paru | grep Version | awk '{print $3}')
    LATEST_VERSION=$(paru -Si paru | grep Version | awk '{print $3}')
    echo "Current PARU version: $CURRENT_VERSION"
    echo "Latest PARU version: $LATEST_VERSION"
    
    if [ "$CURRENT_VERSION" = "$LATEST_VERSION" ]; then
        success "PARU is already up to date."
        exit 0
    else
        warning "Updating PARU from version $CURRENT_VERSION to $LATEST_VERSION..."
    fi
fi
trace makepkg -si --needed --noconfirm -D $PARU_REPO

# test if paru is installed
if ! command -v paru &>/dev/null; then
    error "PARU installation failed. Please check the logs."
fi
success "PARU installed successfully."