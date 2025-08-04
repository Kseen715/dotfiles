info "Installing wleave..."
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm wleave scdoc
trace mkdir -p /home/$DELEVATED_USER/.config/wleave
trace chmod 775 /home/$DELEVATED_USER/.config/wleave
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/wleave
trace cp "$(dirname "$(realpath "$0")")/config/wleave/layout" /home/$DELEVATED_USER/.config/wleave/layout
trace cp "$(dirname "$(realpath "$0")")/config/wleave/style.css" /home/$DELEVATED_USER/.config/wleave/style.css
trace cp "$(dirname "$(realpath "$0")")/config/wleave/lock.png" /home/$DELEVATED_USER/.config/wleave/lock.png
trace cp "$(dirname "$(realpath "$0")")/config/wleave/logout.png" /home/$DELEVATED_USER/.config/wleave/logout.png
trace cp "$(dirname "$(realpath "$0")")/config/wleave/pause.png" /home/$DELEVATED_USER/.config/wleave/pause.png
trace cp "$(dirname "$(realpath "$0")")/config/wleave/power.png" /home/$DELEVATED_USER/.config/wleave/power.png
trace cp "$(dirname "$(realpath "$0")")/config/wleave/restart.png" /home/$DELEVATED_USER/.config/wleave/restart.png
trace cp "$(dirname "$(realpath "$0")")/config/wleave/sleep.png" /home/$DELEVATED_USER/.config/wleave/sleep.png