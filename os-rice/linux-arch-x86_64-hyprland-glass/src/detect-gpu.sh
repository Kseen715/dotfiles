GPU_VENDOR=""
# Detect GPU vendor, including virtualized environments
if command -v lspci &>/dev/null; then
    GPU_VENDOR=$(lspci | grep -E "VGA|3D" | awk -F: '{print $3}' | awk '{print $1}' | sort -u | paste -sd, -)
    if [[ -n "$GPU_VENDOR" ]]; then
        info "Detected GPU vendor(s): $GPU_VENDOR"
    else
        warning "Unknown GPU vendor: $GPU_VENDOR"
    fi
else
    warning "lspci command not found, unable to detect GPU vendor"
fi
