#!/bin/bash

source "$(dirname "$(realpath "$0")")/src/common.sh"

SWAPFILE_SIZE="24G"
SWAPFILE="/swapfile"

info "Checking if swap file is currently mounted..."
if trace "swapon --show | grep -q '$SWAPFILE'"; then
    info "Swap file is currently active, disabling..."
    trace swapoff $SWAPFILE
    if [[ $? -ne 0 ]]; then
        error "Failed to disable swap file $SWAPFILE"
    fi
fi

if [[ -f $SWAPFILE ]]; then
    info "Swap file already exists at $SWAPFILE, removing..."
    trace sudo -u root rm $SWAPFILE
    if [[ $? -ne 0 ]]; then
        error "Failed to remove existing swap file $SWAPFILE"
    fi
fi

info "Setting up swap file with size $SWAPFILE_SIZE..."
trace mkswap -U clear --size $SWAPFILE_SIZE --file $SWAPFILE

if [[ $? -ne 0 ]]; then
    error "Failed to create swap file at $SWAPFILE"
fi

info "Setting permissions on swap file..."
trace chmod 600 $SWAPFILE
if [[ $? -ne 0 ]]; then
    error "Failed to set permissions on swap file $SWAPFILE"
fi

info "Enabling swap file..."
trace swapon $SWAPFILE

# add /swapfile none swap defaults 0 0 to /etc/fstab
# if not already present
if ! grep -q "^$SWAPFILE" /etc/fstab; then
    info "Adding swap file to /etc/fstab..."
    echo "$SWAPFILE none swap defaults 0 0" | tee -a /etc/fstab
    if [[ $? -ne 0 ]]; then
        error "Failed to add swap file to /etc/fstab"
    fi
else
    info "Swap file already present in /etc/fstab"
fi

success "Swap file setup completed successfully"