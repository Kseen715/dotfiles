info "Installing OBS Studio..."
trace pacman -S --needed --noconfirm obs-studio
trace mkdir -p $DELEVATED_USER_HOME/.config/obs-studio
trace chown -R "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.config/obs-studio