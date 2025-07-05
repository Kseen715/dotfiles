info "Installing zsh, dependencies and dotfiles..."
trace cd $DOTFILES_KSEEN715_REPO/zsh
trace chmod +x ./install-run.sh
sudo -u "$DELEVATED_USER" ./install-run.sh -y
trace cd $SCRIPT_DIR