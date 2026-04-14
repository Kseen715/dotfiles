info "Installing btop..."
trace pacman -S --needed --noconfirm btop
info "Installing btop dotfiles..."
trace sudo -u "$DELEVATED_USER" mkdir -p $DELEVATED_USER_HOME/.config/btop
trace chmod 775 $DELEVATED_USER_HOME/.config/btop
trace chown "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.config/btop
trace cp $DOTFILES_KSEEN715_REPO/btop/btop.conf $DELEVATED_USER_HOME/.config/btop/btop.conf
trace chmod 644 $DELEVATED_USER_HOME/.config/btop/btop.conf
trace chown "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.config/btop/btop.conf