# Ensure git is installed
if ! command -v git &>/dev/null; then
    info "Git not found. Installing git..."
    trace pacman -S --needed --noconfirm git wget nano vim man
fi