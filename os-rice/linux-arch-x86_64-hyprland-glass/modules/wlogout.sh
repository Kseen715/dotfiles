info "Installing wlogout..."
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm wlogout
trace mkdir -p $DELEVATED_USER_HOME/.config/wlogout
trace chmod 775 $DELEVATED_USER_HOME/.config/wlogout
trace chown "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.config/wlogout
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/layout" $DELEVATED_USER_HOME/.config/wlogout/layout
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/style.css" $DELEVATED_USER_HOME/.config/wlogout/style.css
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/lock.png" $DELEVATED_USER_HOME/.config/wlogout/lock.png
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/logout.png" $DELEVATED_USER_HOME/.config/wlogout/logout.png
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/pause.png" $DELEVATED_USER_HOME/.config/wlogout/pause.png
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/power.png" $DELEVATED_USER_HOME/.config/wlogout/power.png
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/restart.png" $DELEVATED_USER_HOME/.config/wlogout/restart.png
trace cp "$(dirname "$(realpath "$0")")/config/wlogout/sleep.png" $DELEVATED_USER_HOME/.config/wlogout/sleep.png