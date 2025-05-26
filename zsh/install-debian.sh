#!/bin/bash

# Gather modules
sudo apt update \
&& sudo apt install curl zsh \
&& sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)"

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
