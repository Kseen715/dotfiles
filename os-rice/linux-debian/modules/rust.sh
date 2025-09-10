info "Installing Rust via rustup..."
install_pkg_apt curl
source /home/$DELEVATED_USER/.cargo/env
trace cargo --version
if cargo --version &> /dev/null; then
    info "Rust is already installed, skipping rustup installation."
    # update rust to the latest stable version
    trace "sudo -u $DELEVATED_USER rustup update"
else
    trace "sudo -u $DELEVATED_USER curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sudo -u $DELEVATED_USER sh -s -- -y --default-toolchain stable"
    source /home/$DELEVATED_USER/.cargo/env
    trace . "/home/$DELEVATED_USER/.cargo/env"
    trace cargo --version
fi