info "Installing CurseForge..."
warning "Skipping checksum verification because fck u CurseForge"
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm --nosign curseforge --mflags "--skipchecksums"