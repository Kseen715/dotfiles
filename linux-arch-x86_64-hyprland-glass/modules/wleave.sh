info "Installing wlogout..."
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm wleave-git
trace mkdir -p /home/$DELEVATED_USER/.config/wleave
trace chmod 775 /home/$DELEVATED_USER/.config/wleave
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/wleave
trace cp "$(dirname "$(realpath "$0")")/config/wlogout-wleave/layout" /home/$DELEVATED_USER/.config/wleave/layout
trace cp "$(dirname "$(realpath "$0")")/config/wlogout-wleave/style.css" /home/$DELEVATED_USER/.config/wleave/style.css