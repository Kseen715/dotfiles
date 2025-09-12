info "Installing Rust..."
install_pkg_apt curl
if [[ ":$PATH:" != *":/home/$DELEVATED_USER/.cargo/bin:"* ]]; then
    trace "Adding /home/$DELEVATED_USER/.cargo/bin to PATH"
    PATH="$PATH:/home/$DELEVATED_USER/.cargo/bin"
    export PATH
fi
source /home/$DELEVATED_USER/.cargo/env
trace cargo --version
if cargo --version &> /dev/null; then
    info "Rust is already installed, skipping rustup installation."
    # update rust to the latest stable version
    trace "sudo -u $DELEVATED_USER rustup update"
else
    trace "sudo -u $DELEVATED_USER curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sudo -u $DELEVATED_USER sh -s -- -y --default-toolchain stable"
    trace source /home/$DELEVATED_USER/.cargo/env
    trace . "/home/$DELEVATED_USER/.cargo/env"
    trace sudo -u $DELEVATED_USER rustup default stable
    # trace rustup update
    trace cargo --version
fi