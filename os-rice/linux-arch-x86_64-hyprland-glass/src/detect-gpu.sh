GPU_VENDOR=""
# Detect GPU vendor, including virtualized environments
if command -v lspci &>/dev/null; then
    GPU_VENDOR=$(lspci | grep -E "VGA|3D" | awk -F: '{print $3}' | awk '{print $1}')
    if [[ "$GPU_VENDOR" == "NVIDIA" ]]; then
        info "NVIDIA GPU detected"
    elif [[ "$GPU_VENDOR" == "AMD" ]]; then
        info "AMD GPU detected"
    elif [[ "$GPU_VENDOR" == "Intel" ]]; then
        info "Intel GPU detected"
    elif [[ "$GPU_VENDOR" == "VMware" ]]; then
        info "VMware GPU detected"
    elif [[ "$GPU_VENDOR" == "VirtualBox" ]]; then
        info "VirtualBox GPU detected"
    elif [[ -z "$GPU_VENDOR" ]]; then
        info "No GPU detected, assuming virtualized environment with no dedicated GPU"
    else
        warning "Unknown GPU vendor: $GPU_VENDOR"
    fi
else
    warning "lspci command not found, unable to detect GPU vendor"
fi