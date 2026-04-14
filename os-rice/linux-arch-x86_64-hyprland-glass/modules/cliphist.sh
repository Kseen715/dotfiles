info "Installing cliphist..."
trace pacman -S --needed --noconfirm cliphist ripgrep

trace cp $SCRIPT_DIR/config/hypr/start-cliphist-store.sh $DELEVATED_USER_HOME/.config/hypr/start-cliphist-store.sh
trace chmod +x $DELEVATED_USER_HOME/.config/hypr/start-cliphist-store.sh
trace chown "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.config/hypr/start-cliphist-store.sh
info "Checking if Golang installed..."
if ! command -v go &>/dev/null; then
    info "Golang not found. Installing Golang..."
    trace pacman -S --needed --noconfirm go
else
    info "Golang is installed"
fi
trace sudo -u "$DELEVATED_USER" go install github.com/pdf/cliphist-wofi-img@latest
trace wget https://raw.githubusercontent.com/sentriz/cliphist/refs/heads/master/contrib/cliphist-wofi-img -O /usr/local/bin/cliphist-wofi-img
trace chmod +x /usr/local/bin/cliphist-wofi-img
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /usr/local/bin/cliphist-wofi-img

trace mkdir -p $DELEVATED_USER_HOME/.cache/cliphist/thumbs
trace chmod 755 $DELEVATED_USER_HOME/.cache/cliphist
trace chmod 755 $DELEVATED_USER_HOME/.cache/cliphist/thumbs
trace chown "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.cache/cliphist
trace chown "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.cache/cliphist/thumbs