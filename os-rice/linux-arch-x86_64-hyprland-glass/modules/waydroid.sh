info "Installing waydroid..."
# trace pacman -S --needed --noconfirm
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm waydroid waydroid-image-gapps
# clone https://github.com/casualsnek/waydroid_script.git
trace waydroid init -s GAPPS
WAYDROID_SCRIPT_REPO="/tmp/waydroid_script"
install_or_update_git_repo "waydroid_script" "https://github.com/casualsnek/waydroid_script.git" "$WAYDROID_SCRIPT_REPO" "--depth 1"
trace python3 -m venv --clear "$WAYDROID_SCRIPT_REPO/venv"
trace "$WAYDROID_SCRIPT_REPO/venv/bin/pip" install --upgrade pip
trace "$WAYDROID_SCRIPT_REPO/venv/bin/pip" install -r "$WAYDROID_SCRIPT_REPO/requirements.txt"
# if intel libhoudini, if amd libndk (cpus)
if [ "$CPU_VENDOR" = "GenuineIntel" ]; then
    trace "$WAYDROID_SCRIPT_REPO/venv/bin/python" "$WAYDROID_SCRIPT_REPO/main.py" install libhoudini 
    # trace "$WAYDROID_SCRIPT_REPO/venv/bin/python" "$WAYDROID_SCRIPT_REPO/main.py" install libndk
elif [ "$CPU_VENDOR" = "AuthenticAMD" ]; then
    trace "$WAYDROID_SCRIPT_REPO/venv/bin/python" "$WAYDROID_SCRIPT_REPO/main.py" install libndk
else
    warning "Unsupported CPU vendor for Waydroid: $CPU_VENDOR. ARM compatibility libraries can't be installed"
fi

# Enable Waydroid service
trace sudo systemctl enable --now waydroid-container.service
