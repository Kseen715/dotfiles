info "Installing wlogout..."
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm wlogout
trace mkdir -p /home/$DELEVATED_USER/.config/wlogout
trace chmod 775 /home/$DELEVATED_USER/.config/wlogout
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/wlogout
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/layout" /home/$DELEVATED_USER/.config/wlogout/layout
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/style.css" /home/$DELEVATED_USER/.config/wlogout/style.css
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/lock.png" /home/$DELEVATED_USER/.config/wlogout/lock.png
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/logout.png" /home/$DELEVATED_USER/.config/wlogout/logout.png
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/pause.png" /home/$DELEVATED_USER/.config/wlogout/pause.png
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/power.png" /home/$DELEVATED_USER/.config/wlogout/power.png
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/restart.png" /home/$DELEVATED_USER/.config/wlogout/restart.png
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/sleep.png" /home/$DELEVATED_USER/.config/wlogout/sleep.png