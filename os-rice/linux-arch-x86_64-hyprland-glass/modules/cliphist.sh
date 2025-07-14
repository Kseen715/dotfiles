info "Installing cliphist..."
trace pacman -S --needed --noconfirm cliphist

trace cp $SCRIPT_DIR/config/hypr/start-cliphist-store.sh /home/$DELEVATED_USER/.config/hypr/start-cliphist-store.sh
trace chmod +x /home/$DELEVATED_USER/.config/hypr/start-cliphist-store.sh
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/hypr/start-cliphist-store.sh
info "Checking if Golang installed..."
if ! command -v go &>/dev/null; then
    info "Golang not found. Installing Golang..."
    trace pacman -S --needed --noconfirm go
else
    info "Golang is installed"
fi
trace sudo -u $DELEVATED_USER go install github.com/pdf/cliphist-wofi-img@latest
trace wget https://raw.githubusercontent.com/sentriz/cliphist/refs/heads/master/contrib/cliphist-wofi-img -O /usr/local/bin/cliphist-wofi-img
trace chmod +x /usr/local/bin/cliphist-wofi-img
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /usr/local/bin/cliphist-wofi-img