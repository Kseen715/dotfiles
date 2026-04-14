info "Installing gtklock..."
trace pacman -S --needed --noconfirm gtklock gtklock-userinfo-module
trace mkdir -p "$DELEVATED_USER_HOME/.config/gtklock"
trace chmod 775 "$DELEVATED_USER_HOME/.config/gtklock"
trace chown "$DELEVATED_USER":"$DELEVATED_USER" "$DELEVATED_USER_HOME/.config/gtklock"
trace cp "$(dirname "$(realpath "$0")")/config/gtklock/config.ini" "$DELEVATED_USER_HOME/.config/gtklock/"
trace chmod 644 "$DELEVATED_USER_HOME/.config/gtklock/config.ini"
trace cp "$(dirname "$(realpath "$0")")/config/gtklock/style.css" "$DELEVATED_USER_HOME/.config/gtklock/"

WALLPAPER_PATH="${WALLPAPER_PATH:-wallpaper.jpg}"

trace "sudo -u $DELEVATED_USER sed \"s#{{WALLPAPER_PATH}}#$WALLPAPER_PATH#g\" \"$(dirname \"$(realpath \"$0\")\")/config/gtklock/style.css\" > $DELEVATED_USER_HOME/.config/gtklock/style.css"

trace chmod 644 "$DELEVATED_USER_HOME/.config/gtklock/style.css"

# if file not exists
if [ ! -f "$DELEVATED_USER_HOME/.face" ]; then
    trace cp "$(dirname "$(realpath "$0")")/config/gtklock/.face" "$DELEVATED_USER_HOME/"
fi
trace chmod 644 "$DELEVATED_USER_HOME/.face"