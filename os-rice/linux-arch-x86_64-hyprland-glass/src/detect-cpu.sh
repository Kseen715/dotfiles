# detect cpu vendor and model
CPU_VENDOR="" # GenuineIntel, AuthenticAMD
CPU_MODEL=""
CPU_ARCH=""
if command -v lscpu &>/dev/null; then
    CPU_VENDOR=$(lscpu | grep "Vendor ID" | awk '{print $3}')
    CPU_MODEL=$(lscpu | grep "Model name" | awk -F: '{print $2}' | xargs)
    CPU_ARCH=$(lscpu | grep "Architecture" | awk '{print $2}')
    if [[ -n "$CPU_VENDOR" && -n "$CPU_MODEL" && -n "$CPU_ARCH" ]]; then
        info "Detected CPU vendor: $CPU_VENDOR"
        info "Detected CPU model: $CPU_MODEL"
        info "Detected CPU architecture: $CPU_ARCH"
    else
        warning "Unable to detect CPU vendor, model, or architecture"
    fi
else
    warning "lscpu command not found, unable to detect CPU vendor, model, or architecture"
fi