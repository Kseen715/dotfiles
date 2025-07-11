# Check if yay and/or paru is installed and chose one as preferred AUR helper.
# (paru is preferred if both are installed)
if command -v yay &>/dev/null; then
    info "YAY detected"
    AUR_HELPER="yay"
fi
if command -v paru &>/dev/null; then
    info "PARU detected"
    AUR_HELPER="paru"
fi
if [ -z "$AUR_HELPER" ]; then
    warning "No AUR helper found. Installing PARU as the default AUR helper"
    # Run setup-paru.sh script to install paru
    # run as non-root user to avoid permission issues
    trace bash "$SCRIPT_DIR/build-paru.sh" --delevated "$DELEVATED_USER"
fi
info "Using $AUR_HELPER as the AUR helper"