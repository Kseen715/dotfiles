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

echo "Installing AmneziaVPN Client..."
echo "Installing dependencies..."
trace sudo pacman -S --needed --noconfirm qt6-base qt6-declarative qt6-wayland qt6-websockets qt6-webchannel qt6-webengine qt6-svg qt6-tools qt6-remoteobjects vulkan-headers
# download and install latest version of https://github.com/amnezia-vpn/amnezia-client
trace git clone https://github.com/amnezia-vpn/amnezia-client.git $TMP_FOLDER/amnezia-client --depth 1
trace cd $TMP_FOLDER/amnezia-client
trace git submodule update --init --recursive
echo "Building..."
trace ./deploy/build_linux.sh