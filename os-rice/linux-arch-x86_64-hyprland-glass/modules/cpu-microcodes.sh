# CPU microcodes
# if intel 
if [[ "$CPU_VENDOR" == "GenuineIntel" ]]; then
    info "Installing Intel microcode..."
    trace sudo pacman -S --needed --noconfirm intel-ucode
elif [[ "$CPU_VENDOR" == "AuthenticAMD" ]]; then
    info "Installing AMD microcode..."
    trace sudo pacman -S --needed --noconfirm amd-ucode
else
    warning "Unknown CPU vendor: $CPU_VENDOR. No microcode installed."
fi