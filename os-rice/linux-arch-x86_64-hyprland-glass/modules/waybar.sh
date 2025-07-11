info "Installing waybar..."
trace pacman -S --needed --noconfirm waybar gsimplecal ddcutil
info "Installing waybar dotfiles..."
sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config/waybar
trace cp $SCRIPT_DIR/config/waybar/config.jsonc /home/$DELEVATED_USER/.config/waybar/
trace cp $SCRIPT_DIR/config/waybar/style.css /home/$DELEVATED_USER/.config/waybar/
trace cp $SCRIPT_DIR/config/waybar/waybar-ddc-module.sh /home/$DELEVATED_USER/.config/waybar/
trace chmod +x /home/$DELEVATED_USER/.config/waybar/waybar-ddc-module.sh
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/waybar/waybar-ddc-module.sh