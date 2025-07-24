info "Installing btop..."
trace pacman -S --needed --noconfirm btop
info "Installing btop dotfiles..."
trace sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config/btop
trace chmod 775 /home/$DELEVATED_USER/.config/btop
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/btop
trace cp $DOTFILES_KSEEN715_REPO/btop/btop.conf /home/$DELEVATED_USER/.config/btop/btop.conf
trace chmod 644 /home/$DELEVATED_USER/.config/btop/btop.conf
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/btop/btop.conf