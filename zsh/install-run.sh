#!/bin/bash

# Detect package manager and install required packages
install_packages() {
    echo "Installing required packages..."
    local PKGS="curl zsh"
    local PKGS_GENTOO="net-misc/curl app-shells/zsh"

    if command -v apt &> /dev/null; then
        # Debian/Ubuntu
        sudo apt update && sudo apt install -y $PKGS
    elif command -v dnf &> /dev/null; then
        # Fedora/RHEL
        sudo dnf install -y $PKGS
    elif command -v yum &> /dev/null; then
        # CentOS/RHEL/Fedora legacy
        sudo yum install -y $PKGS
    elif command -v pacman &> /dev/null; then
        # Arch Linux
        sudo pacman -S --needed --noconfirm $PKGS
    elif command -v zypper &> /dev/null; then
        # openSUSE
        sudo zypper install -y $PKGS
    elif command -v brew &> /dev/null; then
        # macOS (Homebrew)
        brew install $PKGS
    elif command -v apk &> /dev/null; then
        # Alpine Linux
        sudo apk add $PKGS
    elif command -v port &> /dev/null; then
        # macOS (MacPorts)
        sudo port install $PKGS
    elif command -v emerge &> /dev/null; then
        # Gentoo
        sudo emerge --ask $PKGS_GENTOO
    elif command -v xbps-install &> /dev/null; then
        # Void Linux
        sudo xbps-install -y $PKGS
    else
        echo "Unsupported package manager. Please install $PKGS manually."
        exit 1
    fi
}

install_packages

# Get the install script for Oh My Zsh and run it
sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)"

# Check if cargo is available
if ! command -v cargo &> /dev/null
then
    echo "Cargo not found, installing Rust..."
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
    source "$HOME/.cargo/env"
    echo "Rust installed."
else
    echo "Cargo already installed, skipping Rust installation."
fi

# Install starship and run install script
cargo install starship --locked

# Update configs
./install.sh -y \
&& sudo chsh -s $(which zsh)
