# Ensure git is installed
if ! command -v git &>/dev/null; then
    info "Git not found. Installing git..."
    trace pacman -S --needed --noconfirm git 
fi
trace pacman -S --needed --noconfirm wget nano vim man