info "Installing zsh, dependencies and dotfiles..."

install_pkg_dnf git curl zsh lsd

source "$(dirname "$(realpath "$0")")/modules/rust.sh"

install_pkg_cargo_locked starship

# Get the install script for Oh My Zsh and run it
# if The $ZSH folder already exists (/home/kseen/.oh-my-zsh). skip installation.
if [ -d "/home/$DELEVATED_USER/.oh-my-zsh" ]; then
    info "Oh My Zsh is already installed, skipping installation."
else
    sudo -u "$DELEVATED_USER" sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh | sed "s:env zsh -l::g" | sed "s:chsh -s .*$:true:g")" "" --unattended --skip-chsh
    check_error $? "Failed to install Oh My Zsh"
fi

# Function to install or update zsh plugins
install_or_update_zsh_plugin() {
    local plugin_name="$1"
    local plugin_repo="$2"
    local plugin_dir="/home/$DELEVATED_USER/.oh-my-zsh/custom/plugins/$plugin_name"
    
    if [ -d "$plugin_dir" ]; then
        info "$plugin_name plugin directory exists, checking repository..."
        local current_remote=$(git -C "$plugin_dir" remote get-url origin 2>/dev/null || echo "")
        info "Current remote for $plugin_name: $current_remote"
        if [ "$current_remote" = "$plugin_repo" ] || [ "$current_remote" = "$plugin_repo.git" ] || [ "$current_remote" = "${plugin_repo%.git}" ]; then
            info "Updating $plugin_name plugin..."
            if ! trace "git -C "$plugin_dir" diff --quiet" || ! trace "git -C "$plugin_dir" diff --cached --quiet"; then
                info "Changes detected in repository. Resetting to clean state..."
                trace git -C "$plugin_dir" reset --hard HEAD && trace git -C "$plugin_dir" clean -fd
            fi
            trace git -C "$plugin_dir" pull
            check_error $? "Failed to update $plugin_name plugin"
        else
            info "Different repository found for $plugin_name, removing and cloning correct one..."
            trace rm -rf "$plugin_dir"
            check_error $? "Failed to remove existing $plugin_name plugin"
            trace git clone "$plugin_repo" "$plugin_dir" --depth 1
            check_error $? "Failed to clone $plugin_name plugin"
        fi
    else
        info "Installing $plugin_name plugin..."
        trace git clone "$plugin_repo" "$plugin_dir" --depth 1
        check_error $? "Failed to clone $plugin_name plugin"
    fi
}

# Update configs
trace sudo -u $DELEVATED_USER cp -f "$DOTFILES_KSEEN715_REPO/zsh/.zshrc" "/home/$DELEVATED_USER/.zshrc"
check_error $? "Failed to copy .zshrc to /home/$DELEVATED_USER/"
trace cp -rf "$DOTFILES_KSEEN715_REPO/zsh/.oh-my-zsh" "/home/$DELEVATED_USER/"
check_error $? "Failed to copy .oh-my-zsh to /home/$DELEVATED_USER/"
trace sudo -u $DELEVATED_USER mkdir -p "/home/$DELEVATED_USER/.config"
check_error $? "Failed to create /home/$DELEVATED_USER/.config directory"
trace sudo -u $DELEVATED_USER cp -f $DOTFILES_KSEEN715_REPO/starship/starship.toml "/home/$DELEVATED_USER/.config/starship.toml"
check_error $? "Failed to copy starship.toml to /home/$DELEVATED_USER/.config/"

trace chown -R $DELEVATED_USER:$DELEVATED_USER "/home/$DELEVATED_USER/.oh-my-zsh"
check_error $? "Failed to change ownership of /home/$DELEVATED_USER/.oh-my-zsh"

# Install or update zsh plugins
install_or_update_zsh_plugin "zsh-autosuggestions" "https://github.com/zsh-users/zsh-autosuggestions"
install_or_update_zsh_plugin "zsh-syntax-highlighting" "https://github.com/zsh-users/zsh-syntax-highlighting.git"

# change shell to zsh for the user
trace chsh -s $(which zsh) "$DELEVATED_USER"
check_error $? "Failed to change default shell to zsh for user $DELEVATED_USER"
