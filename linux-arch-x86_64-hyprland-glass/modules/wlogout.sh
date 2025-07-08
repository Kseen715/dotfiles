info "Installing wlogout..."
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm wlogout
trace mkdir -p /home/$DELEVATED_USER/.config/wlogout
trace chmod 775 /home/$DELEVATED_USER/.config/wlogout
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/wlogout