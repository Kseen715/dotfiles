info "Installing kate..."
trace pacman -S --needed --noconfirm kate
trace mkdir -p $DELEVATED_USER_HOME/.config/
trace chmod 775 $DELEVATED_USER_HOME/.config/
trace chown "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.config
trace cp $DOTFILES_KSEEN715_REPO/kate/katerc $DELEVATED_USER_HOME/.config/
trace chmod 644 $DELEVATED_USER_HOME/.config/katerc