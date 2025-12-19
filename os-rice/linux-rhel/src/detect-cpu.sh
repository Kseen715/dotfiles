# detect cpu vendor and model
CPU_VENDOR="" # GenuineIntel, AuthenticAMD
CPU_MODEL=""
CPU_ARCH=""
CPU_CORES=0
if command -v lscpu &>/dev/null; then
    CPU_VENDOR=$(lscpu | grep "Vendor ID" | awk '{print $3}')
    check_error $? "Failed to detect CPU vendor"
    CPU_MODEL=$(lscpu | grep "Model name" | awk -F: '{print $2}' | xargs)
    check_error $? "Failed to detect CPU model"
    CPU_ARCH=$(lscpu | grep "Architecture" | awk '{print $2}')
    check_error $? "Failed to detect CPU architecture"
    CPU_CORES=$(lscpu | grep "^CPU(s):" | awk '{print $2}')
    check_error $? "Failed to detect CPU cores"
    if [[ -n "$CPU_VENDOR" && -n "$CPU_MODEL" && -n "$CPU_ARCH" ]]; then
        info "CPU vendor: $CPU_VENDOR"
        info "CPU model: $CPU_MODEL"
        info "CPU architecture: $CPU_ARCH"
        info "CPU cores: $CPU_CORES"
    else
        warning "Unable to detect CPU vendor, model, or architecture"
    fi
else
    warning "lscpu command not found, unable to detect CPU vendor, model, or architecture"
fi