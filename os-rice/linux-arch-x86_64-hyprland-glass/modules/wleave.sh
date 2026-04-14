info "Installing wleave..."
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm wleave scdoc
trace mkdir -p $DELEVATED_USER_HOME/.config/wleave
trace chmod 775 $DELEVATED_USER_HOME/.config/wleave
trace chown "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.config/wleave
trace cp "$(dirname "$(realpath "$0")")/config/wleave/layout" $DELEVATED_USER_HOME/.config/wleave/layout
trace cp "$(dirname "$(realpath "$0")")/config/wleave/style.css" $DELEVATED_USER_HOME/.config/wleave/style.css
trace cp "$(dirname "$(realpath "$0")")/config/wleave/lock.png" $DELEVATED_USER_HOME/.config/wleave/lock.png
trace cp "$(dirname "$(realpath "$0")")/config/wleave/logout.png" $DELEVATED_USER_HOME/.config/wleave/logout.png
trace cp "$(dirname "$(realpath "$0")")/config/wleave/pause.png" $DELEVATED_USER_HOME/.config/wleave/pause.png
trace cp "$(dirname "$(realpath "$0")")/config/wleave/power.png" $DELEVATED_USER_HOME/.config/wleave/power.png
trace cp "$(dirname "$(realpath "$0")")/config/wleave/restart.png" $DELEVATED_USER_HOME/.config/wleave/restart.png
trace cp "$(dirname "$(realpath "$0")")/config/wleave/sleep.png" $DELEVATED_USER_HOME/.config/wleave/sleep.png