info "Installing waybar..."
trace pacman -S --needed --noconfirm waybar gsimplecal ddcutil
info "Installing waybar dotfiles..."
sudo -u "$DELEVATED_USER" mkdir -p $DELEVATED_USER_HOME/.config/waybar
trace cp $SCRIPT_DIR/config/waybar/config.jsonc $DELEVATED_USER_HOME/.config/waybar/
trace cp $SCRIPT_DIR/config/waybar/style.css $DELEVATED_USER_HOME/.config/waybar/
trace cp $SCRIPT_DIR/config/waybar/waybar-ddc-module.sh $DELEVATED_USER_HOME/.config/waybar/
trace chmod +x $DELEVATED_USER_HOME/.config/waybar/waybar-ddc-module.sh
trace chown "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.config/waybar/waybar-ddc-module.sh