GPU_VENDOR=""
GPU_MODEL=""
GPU_COUNT=0

# Function to normalize vendor names
normalize_vendor() {
    local vendor="$1"
    case "$vendor" in
        *"NVIDIA"*|*"nVidia"*|*"GeForce"*|*"Quadro"*|*"Tesla"*|*"RTX"*|*"GTX"*)
            echo "NVIDIA"
            ;;
        *"AMD"*|*"ATI"*|*"Radeon"*|*"FirePro"*|*"FireGL"*)
            echo "AMD"
            ;;
        *"Intel"*|*"HD Graphics"*|*"UHD Graphics"*|*"Iris"*|*"Arc"*)
            echo "Intel"
            ;;
        *"VMware"*|*"VMWARE"*)
            echo "VMware"
            ;;
        *"VirtualBox"*|*"VBOX"*)
            echo "VirtualBox"
            ;;
        *"QEMU"*|*"virtio"*)
            echo "QEMU"
            ;;
        *"Matrox"*)
            echo "Matrox"
            ;;
        *"Red Hat"*|*"QXL"*)
            echo "RedHat"
            ;;
        *"Microsoft"*|*"Hyper-V"*)
            echo "Microsoft"
            ;;
        *"Cirrus Logic"*)
            echo "Cirrus"
            ;;
        *"S3"*)
            echo "S3"
            ;;
        *"SiS"*)
            echo "SiS"
            ;;
        *"VIA"*)
            echo "VIA"
            ;;
        *)
            echo "Unknown"
            ;;
    esac
}

# Detect GPU vendor and model
if command -v lspci &>/dev/null; then
    # Get all GPU devices (VGA compatible controllers and 3D controllers)
    gpu_devices=$(lspci -mm | grep -E "VGA compatible controller|3D controller")
    
    if [[ -n "$gpu_devices" ]]; then
        declare -A vendor_count
        declare -a detected_vendors
        declare -a detected_models
        
        # Process each GPU device
        while IFS= read -r line; do
            # Extract vendor and device information
            # lspci -mm format: "slot" "class" "vendor" "device" "subsystem_vendor" "subsystem_device"
            vendor=$(echo "$line" | cut -d'"' -f6)
            device=$(echo "$line" | cut -d'"' -f8)
            
            # Fallback to regular lspci format if -mm parsing fails
            if [[ -z "$vendor" || -z "$device" ]]; then
                # Try alternative parsing
                gpu_info=$(echo "$line" | sed 's/.*: //')
                vendor=$(echo "$gpu_info" | awk '{print $1}')
                device=$(echo "$gpu_info" | sed "s/^$vendor //")
            fi
            
            if [[ -n "$vendor" ]]; then
                normalized_vendor=$(normalize_vendor "$vendor $device")
                
                # Count occurrences of each vendor
                if [[ -z "${vendor_count[$normalized_vendor]}" ]]; then
                    vendor_count[$normalized_vendor]=1
                    detected_vendors+=("$normalized_vendor")
                else
                    ((vendor_count[$normalized_vendor]++))
                fi
                
                # Store full model information
                if [[ -n "$device" ]]; then
                    detected_models+=("$vendor $device")
                else
                    detected_models+=("$vendor")
                fi
                
                ((GPU_COUNT++))
            fi
        done <<< "$gpu_devices"
        
        # Create comma-separated vendor list (for backward compatibility)
        if [[ ${#detected_vendors[@]} -gt 0 ]]; then
            GPU_VENDOR=$(IFS=","; echo "${detected_vendors[*]}")
            GPU_MODEL=$(IFS=" | "; echo "${detected_models[*]}")
            
            info "Detected $GPU_COUNT GPU(s):"
            for vendor in "${detected_vendors[@]}"; do
                count=${vendor_count[$vendor]}
                if [[ $count -eq 1 ]]; then
                    info "  - $vendor GPU"
                else
                    info "  - $vendor GPU (x$count)"
                fi
            done
            
            if [[ -n "$GPU_MODEL" ]]; then
                info "GPU model(s): $GPU_MODEL"
            fi
        else
            warning "No GPU vendors could be identified"
        fi
    else
        warning "No GPU devices found"
    fi
    
    # Additional detection methods for edge cases
    if [[ -z "$GPU_VENDOR" ]] && command -v glxinfo &>/dev/null; then
        info "Trying alternative detection with glxinfo..."
        renderer=$(glxinfo | grep "OpenGL renderer string" | cut -d: -f2- | xargs)
        if [[ -n "$renderer" ]]; then
            normalized_vendor=$(normalize_vendor "$renderer")
            if [[ "$normalized_vendor" != "Unknown" ]]; then
                GPU_VENDOR="$normalized_vendor"
                GPU_MODEL="$renderer"
                GPU_COUNT=1
                info "Detected GPU via OpenGL: $normalized_vendor"
                info "GPU model: $renderer"
            fi
        fi
    fi
    
    # Try /proc/driver/nvidia/version for NVIDIA detection
    if [[ -z "$GPU_VENDOR" ]] && [[ -f "/proc/driver/nvidia/version" ]]; then
        info "NVIDIA driver detected via /proc filesystem"
        GPU_VENDOR="NVIDIA"
        GPU_COUNT=1
        if command -v nvidia-smi &>/dev/null; then
            GPU_MODEL=$(nvidia-smi --query-gpu=name --format=csv,noheader,nounits | head -1 2>/dev/null)
            if [[ -n "$GPU_MODEL" ]]; then
                info "GPU model: $GPU_MODEL"
            fi
        fi
    fi
    
    # Try /sys/class/drm for additional detection
    if [[ -z "$GPU_VENDOR" ]] && [[ -d "/sys/class/drm" ]]; then
        info "Trying detection via DRM subsystem..."
        for card in /sys/class/drm/card*/device/vendor; do
            if [[ -f "$card" ]]; then
                vendor_id=$(cat "$card" 2>/dev/null)
                device_id=$(cat "$(dirname "$card")/device" 2>/dev/null)
                
                case "$vendor_id" in
                    "0x10de") # NVIDIA
                        GPU_VENDOR="NVIDIA"
                        GPU_COUNT=$((GPU_COUNT + 1))
                        ;;
                    "0x1002") # AMD
                        GPU_VENDOR="AMD"
                        GPU_COUNT=$((GPU_COUNT + 1))
                        ;;
                    "0x8086") # Intel
                        GPU_VENDOR="Intel"
                        GPU_COUNT=$((GPU_COUNT + 1))
                        ;;
                    "0x15ad") # VMware
                        GPU_VENDOR="VMware"
                        GPU_COUNT=$((GPU_COUNT + 1))
                        ;;
                esac
            fi
        done
        
        if [[ -n "$GPU_VENDOR" ]]; then
            info "Detected GPU via DRM: $GPU_VENDOR"
        fi
    fi
    
else
    warning "lspci command not found, unable to detect GPU vendor"
    
    # Try alternative detection methods when lspci is not available
    if command -v glxinfo &>/dev/null; then
        info "Trying detection with glxinfo..."
        renderer=$(glxinfo | grep "OpenGL renderer string" | cut -d: -f2- | xargs 2>/dev/null)
        if [[ -n "$renderer" ]]; then
            normalized_vendor=$(normalize_vendor "$renderer")
            if [[ "$normalized_vendor" != "Unknown" ]]; then
                GPU_VENDOR="$normalized_vendor"
                GPU_MODEL="$renderer"
                GPU_COUNT=1
                info "Detected GPU: $normalized_vendor"
                info "GPU model: $renderer"
            fi
        fi
    elif [[ -f "/proc/driver/nvidia/version" ]]; then
        GPU_VENDOR="NVIDIA"
        GPU_COUNT=1
        info "NVIDIA GPU detected via /proc filesystem"
    fi
fi

# Final validation and summary
if [[ -n "$GPU_VENDOR" ]]; then
    info "Final GPU detection result:"
    info "  Vendor(s): $GPU_VENDOR"
    if [[ -n "$GPU_MODEL" ]]; then
        info "  Model(s): $GPU_MODEL"
    fi
    info "  Count: $GPU_COUNT"
else
    warning "Unable to detect any GPU vendor"
    GPU_VENDOR="Unknown"
    GPU_COUNT=0
fi

# Export variables for use by other scripts
export GPU_VENDOR
export GPU_MODEL
export GPU_COUNT