info "Installing Rust..."

install_pkg_dnf curl gcc

# Add cargo bin to PATH (may not exist yet on a fresh install — that is fine)
if [[ ":$PATH:" != *":$DELEVATED_USER_HOME/.cargo/bin:"* ]]; then
    PATH="$PATH:$DELEVATED_USER_HOME/.cargo/bin"
    export PATH
fi

# Source cargo env only if it already exists (i.e. Rust was installed previously)
if [[ -f "$DELEVATED_USER_HOME/.cargo/env" ]]; then
    source "$DELEVATED_USER_HOME/.cargo/env"
fi

if sudo -u "$DELEVATED_USER" "$DELEVATED_USER_HOME/.cargo/bin/cargo" --version &>/dev/null; then
    info "Rust is already installed, updating..."
    trace sudo -u "$DELEVATED_USER" "$DELEVATED_USER_HOME/.cargo/bin/rustup" update
    check_error $? "Failed to update rustup"
else
    info "Rust not found, installing via rustup..."
    trace "sudo -u \"$DELEVATED_USER\" curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sudo -u \"$DELEVATED_USER\" sh -s -- -y --default-toolchain stable"
    check_error $? "Failed to install rustup"

    # Source the newly-created cargo env so subsequent commands in this session can use cargo
    source "$DELEVATED_USER_HOME/.cargo/env"

    trace sudo -u "$DELEVATED_USER" "$DELEVATED_USER_HOME/.cargo/bin/rustup" default stable
    check_error $? "Failed to set rustup default toolchain"

    trace sudo -u "$DELEVATED_USER" "$DELEVATED_USER_HOME/.cargo/bin/cargo" --version
    check_error $? "Failed to verify cargo installation"
fi