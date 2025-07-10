info "Installing gtklock..."
trace pacman -S --needed --noconfirm gtklock gtklock-userinfo-module
trace mkdir -p /home/$DELEVATED_USER/.config/gtklock
trace chmod 775 /home/$DELEVATED_USER/.config/gtklock
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/gtklock
trace cp "$(dirname "$(realpath "$0")")/config/gtklock/config.ini" /home/$DELEVATED_USER/.config/gtklock/
trace chmod 644 /home/$DELEVATED_USER/.config/gtklock/config.ini
trace cp "$(dirname "$(realpath "$0")")/config/gtklock/style.css" /home/$DELEVATED_USER/.config/gtklock/

WALLPAPER_PATH="${WALLPAPER_PATH:-wallpaper.jpg}"

trace "sudo -u $DELEVATED_USER sed \"s#{{WALLPAPER_PATH}}#$WALLPAPER_PATH#g\" \"$(dirname \"$(realpath \"$0\")\")/config/gtklock/style.css\" > /home/$DELEVATED_USER/.config/gtklock/style.css"

trace chmod 644 /home/$DELEVATED_USER/.config/gtklock/style.css

trace cp "$(dirname "$(realpath "$0")")/config/gtklock/.face" /home/$DELEVATED_USER/
trace chmod 644 /home/$DELEVATED_USER/.face