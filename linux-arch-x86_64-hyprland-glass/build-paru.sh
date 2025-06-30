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

# Folder for temp repos
TMP_FOLDER="/tmp/setup"
trace mkdir -p $TMP_FOLDER

# Deps for stuff
echo "Installing dependencies for setup..."
trace pacman -S --needed --noconfirm base-devel git

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

if [ -n "$DELEVATED_USER" ]; then
    trace chown -R "$DELEVATED_USER":"$DELEVATED_USER" $PARU_REPO
fi
trace sudo -u "$DELEVATED_USER" makepkg -si --needed --noconfirm -D $PARU_REPO

# test if paru is installed
if ! command -v paru &>/dev/null; then
    error "PARU installation failed. Please check the logs."
fi
success "PARU installed successfully."