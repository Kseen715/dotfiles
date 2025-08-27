info "Installing Rust via rustup..."
install_pkg_apt curl
trace "sudo -u $DELEVATED_USER curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --default-toolchain stable"
trace source /home/$DELEVATED_USER/.cargo/env