info "Installing zsh, dependencies and dotfiles..."
trace cd $DOTFILES_KSEEN715_REPO/zsh
trace chmod +x ./install-run.sh
sudo -u "$DELEVATED_USER" $DOTFILES_KSEEN715_REPO/zsh/install-run.sh -y
trace cd $SCRIPT_DIR
# change shell to zsh for the user
chsh -s $(which zsh) "$DELEVATED_USER"