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
        # use absolute path to the script to avoid issues with relative paths
        if [[ ! -f "$SCRIPT_DIR/$(basename "$0")" ]]; then
            error "Script not found at expected location: $SCRIPT_DIR/$(basename "$0")"
        fi
        # Re-executes the script with sudo
        trace chmod +x "$SCRIPT_DIR/$(basename "$0")"
        exec sudo "$SCRIPT_DIR/$(basename "$0")" "$@"
        exit 0
    fi
fi

# ==============================================================================

echo "Installing required packages..."
trace pacman -Sy --needed pacman-contrib curl --noconfirm

echo "Backuping current mirrorlist..."
if [[ -f /etc/pacman.d/mirrorlist ]]; then
    trace cp /etc/pacman.d/mirrorlist /etc/pacman.d/mirrorlist.backup
    success "Backup created at /etc/pacman.d/mirrorlist.backup"
else
    warning "No existing mirrorlist found"
    echo "Creating a new mirrorlist..."
    trace mkdir -p /etc/pacman.d
    curl -o /etc/pacman.d/mirrorlist https://archlinux.org/mirrorlist/all/
    error "Edit the mirrorlist at /etc/pacman.d/mirrorlist to choose your preferred mirrors."
fi

echo "Ranking mirrors..."
rankmirrors -n 16 /etc/pacman.d/mirrorlist.backup > /etc/pacman.d/mirrorlist
trace pacman -Syy --noconfirm