VIRT=""
# Detect virtualizations (VMware, VirtualBox, QEMU, etc.)
if command -v lscpu &>/dev/null; then
    if lscpu | grep -q "VMware"; then
        info "VMware detected"
        VIRT="vmware"
    elif lscpu | grep -q "VirtualBox"; then
        info "VirtualBox detected"
        VIRT="virtualbox"
    elif lscpu | grep -q "QEMU"; then
        info "QEMU detected"
        VIRT="qemu"
    fi
fi