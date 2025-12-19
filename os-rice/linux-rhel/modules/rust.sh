info "Installing Rust..."

install_pkg_dnf curl gcc

if [[ ":$PATH:" != *":/home/$DELEVATED_USER/.cargo/bin:"* ]]; then
    PATH="$PATH:/home/$DELEVATED_USER/.cargo/bin"
    export PATH
fi

source /home/$DELEVATED_USER/.cargo/env
trace sudo -u $DELEVATED_USER /home/$DELEVATED_USER/.cargo/bin/cargo --version
check_error $? "Failed to verify cargo installation"

if sudo -u $DELEVATED_USER /home/$DELEVATED_USER/.cargo/bin/cargo --version &> /dev/null; then
    info "Rust is already installed, skipping rustup installation."
    trace "sudo -u $DELEVATED_USER /home/$DELEVATED_USER/.cargo/bin/rustup update"
    check_error $? "Failed to update rustup"
else
    trace "sudo -u $DELEVATED_USER curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sudo -u $DELEVATED_USER sh -s -- -y --default-toolchain stable"
    check_error $? "Failed to install rustup"
    trace sudo -u $DELEVATED_USER /home/$DELEVATED_USER/.cargo/bin/rustup default stable
    check_error $? "Failed to set rustup default toolchain"
    trace sudo -u $DELEVATED_USER /home/$DELEVATED_USER/.cargo/bin/cargo --version
    check_error $? "Failed to verify cargo installation after rustup installation"
fi