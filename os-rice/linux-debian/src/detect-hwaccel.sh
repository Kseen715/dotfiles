# v0.1.1

GPU_VENDOR=""
GPU_MODEL=""
GPU_COUNT=0

NPU_VENDOR=""
NPU_MODEL=""
NPU_COUNT=0

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

# === GPU ===
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
    fi
    
    # Additional detection methods for edge cases
    if [[ -z "$GPU_VENDOR" ]] && command -v glxinfo &>/dev/null; then
        # info "Trying alternative detection with glxinfo..."
        renderer=$(glxinfo 2>/dev/null | grep "OpenGL renderer string" | cut -d: -f2- | xargs)
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
        # info "NVIDIA driver detected via /proc filesystem"
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
        # info "Trying detection via DRM subsystem..."
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
        # info "Trying detection with glxinfo..."
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
    info "No GPU detected"
    GPU_VENDOR="Unknown"
    GPU_COUNT=0
fi
# === /GPU ===

# === NPU ===

SYS_NAME=""
SYS_NAME_DETECTED=0
detect_system_name_inxi() {
    if [[ $SYS_NAME_DETECTED -eq 1 ]]; then
        return
    fi
    if command -v inxi &>/dev/null; then
        SYS_NAME="$(inxi -zM --indents=0 --tty 2>/dev/null | grep "System:" | sed 's/.*System:[[:space:]]*//' | sed 's/[[:space:]]*details:.*//')"
        SYS_NAME_DETECTED=1
    fi
}

# Detect NPU vendor and model
if command -v dmesg &>/dev/null; then
    # Get dmesg output and look for NPU-related entries
    dmesg_output=$(dmesg 2>/dev/null | grep -i -E "\bnpu\b|rknpu|neural.*processing|rockchip.*npu\b|mali.*npu|ethos.*npu|hexagon.*npu|qualcomm.*npu|intel.*npu|mediatek.*npu|amlogic.*npu")
    
    if [[ -n "$dmesg_output" ]]; then
        declare -A npu_vendor_count
        declare -a detected_npu_vendors
        declare -a detected_npu_models
        declare -A unique_npu_devices  # Track unique NPU devices by address/identifier
        
        # Process each NPU-related dmesg line
        while IFS= read -r line; do
            npu_vendor=""
            npu_model=""
            device_identifier=""
            
            # For Rockchip NPU, always use a consistent identifier regardless of device address
            if echo "$line" | grep -i -q "rknpu\|rockchip.*npu"; then
                device_identifier="rockchip_npu_0"
            else
                # Extract device identifier (address) from dmesg line for other vendors
                device_addr=$(echo "$line" | grep -o "[0-9a-f]\{6,8\}\.npu")
                if [[ -n "$device_addr" ]]; then
                    device_identifier="$device_addr"
                else
                    # For other vendors, use a cleaned version as identifier
                    device_identifier=$(echo "$line" | sed "s/\[.*\] *//" | sed "s/^[[:space:]]*//" | awk "{print \$1 \$2}" | head -c 30)
                fi
            fi
            
            # Skip lines that are just power supply lookups or property failures (noise)
            if echo "$line" | grep -i -q "looking up.*supply\|supply.*property.*failed\|could not add device link\|debugfs.*already present"; then
                continue
            fi
            
            # Rockchip NPU detection
            if echo "$line" | grep -i -q "rknpu\|rockchip.*npu"; then
                npu_vendor="Rockchip"
                # Universal method: extract NPU model from system name (2nd word)
                detect_system_name_inxi
                if [[ -n "$SYS_NAME" ]]; then
                    # Extract the second word from system name (should be the SoC model)
                    soc_model=$(echo "$SYS_NAME" | awk '{print $2}')
                    if [[ -n "$soc_model" && "$soc_model" =~ ^RK[0-9] ]]; then
                        npu_model="$(echo "$soc_model" | tr '[:lower:]' '[:upper:]') NPU"
                    else
                        # Fallback: extract from dmesg line
                        model_match=$(echo "$line" | grep -o -i "rk[0-9]\+[a-z]*")
                        if [[ -n "$model_match" ]]; then
                            npu_model="$(echo "$model_match" | tr '[:lower:]' '[:upper:]') NPU"
                        else
                            npu_model="Rockchip NPU"
                        fi
                    fi
                else
                    # Fallback: extract from dmesg line
                    model_match=$(echo "$line" | grep -o -i "rk[0-9]\+[a-z]*")
                    if [[ -n "$model_match" ]]; then
                        npu_model="$(echo "$model_match" | tr '[:lower:]' '[:upper:]') NPU"
                    else
                        npu_model="Rockchip NPU"
                    fi
                fi
            # Qualcomm NPU detection
            elif echo "$line" | grep -i -q "qualcomm.*npu\|hexagon"; then
                npu_vendor="Qualcomm"
                if echo "$line" | grep -i -q "hexagon"; then
                    npu_model="Hexagon NPU"
                else
                    npu_model="Qualcomm NPU"
                fi
            # Intel NPU detection
            elif echo "$line" | grep -i -q "intel.*npu"; then
                npu_vendor="Intel"
                npu_model="Intel NPU"
            # MediaTek NPU detection
            elif echo "$line" | grep -i -q "mediatek.*npu"; then
                npu_vendor="MediaTek"
                npu_model="MediaTek NPU"
            # Amlogic NPU detection
            elif echo "$line" | grep -i -q "amlogic.*npu"; then
                npu_vendor="Amlogic"
                npu_model="Amlogic NPU"
            # ARM Mali NPU detection
            elif echo "$line" | grep -i -q "mali.*npu\|ethos"; then
                npu_vendor="ARM"
                if echo "$line" | grep -i -q "ethos"; then
                    npu_model="ARM Ethos NPU"
                else
                    npu_model="ARM Mali NPU"
                fi
            # Generic neural/npu detection
            elif echo "$line" | grep -i -q "neural\|npu"; then
                npu_vendor="Unknown"
                npu_model="Unknown NPU"
            fi
            
            if [[ -n "$npu_vendor" && -n "$device_identifier" ]]; then
                # Only count each unique device once
                device_key="${npu_vendor}_${device_identifier}"
                if [[ -z "${unique_npu_devices[$device_key]}" ]]; then
                    unique_npu_devices[$device_key]=1
                    
                    # Count occurrences of each vendor
                    if [[ -z "${npu_vendor_count[$npu_vendor]}" ]]; then
                        npu_vendor_count[$npu_vendor]=1
                        detected_npu_vendors+=("$npu_vendor")
                    else
                        ((npu_vendor_count[$npu_vendor]++))
                    fi
                    
                    # Store full model information (only once per unique device)
                    if [[ -n "$npu_model" ]]; then
                        detected_npu_models+=("$npu_model")
                    fi
                    
                    ((NPU_COUNT++))
                fi
            fi
        done <<< "$dmesg_output"
        
        # Create comma-separated vendor list
        if [[ ${#detected_npu_vendors[@]} -gt 0 ]]; then
            NPU_VENDOR=$(IFS=","; echo "${detected_npu_vendors[*]}")
            NPU_MODEL=$(IFS=" | "; echo "${detected_npu_models[*]}")
            
            info "Detected $NPU_COUNT NPU(s) via dmesg:"
            for vendor in "${detected_npu_vendors[@]}"; do
                count=${npu_vendor_count[$vendor]}
                if [[ $count -eq 1 ]]; then
                    info "  - $vendor NPU"
                else
                    info "  - $vendor NPU (x$count)"
                fi
            done
            
            if [[ -n "$NPU_MODEL" ]]; then
                info "NPU model(s): $NPU_MODEL"
            fi
        fi
    fi
else
    warning "dmesg command not found, unable to detect NPU via kernel messages"
fi

# Additional NPU detection methods
# Check for NPU-specific device files and drivers
if [[ -z "$NPU_VENDOR" ]]; then
    # info "Trying additional NPU detection methods..."
    
    # Check for Rockchip NPU device files
    if [[ -c "/dev/rknpu" ]] || [[ -d "/sys/class/rknpu" ]]; then
        NPU_VENDOR="Rockchip"
        NPU_MODEL="Rockchip NPU"
        NPU_COUNT=1
        info "Rockchip NPU detected via device files"
    fi
    
    # Check for loaded NPU kernel modules
    if command -v lsmod &>/dev/null; then
        npu_modules=$(lsmod | grep -i -E "rknpu|npu|neural")
        if [[ -n "$npu_modules" ]]; then
            while IFS= read -r module_line; do
                module_name=$(echo "$module_line" | awk '{print $1}')
                case "$module_name" in
                    *rknpu*|*rockchip*npu*)
                        if [[ -z "$NPU_VENDOR" ]]; then
                            NPU_VENDOR="Rockchip"
                            NPU_MODEL="Rockchip NPU"
                            NPU_COUNT=1
                            info "Rockchip NPU detected via kernel module: $module_name"
                        fi
                        ;;
                    *npu*|*neural*)
                        if [[ -z "$NPU_VENDOR" ]]; then
                            NPU_VENDOR="Unknown"
                            NPU_MODEL="Unknown NPU"
                            NPU_COUNT=1
                            info "NPU detected via kernel module: $module_name"
                        fi
                        ;;
                esac
            done <<< "$npu_modules"
        fi
    fi
    
    # Check /proc/device-tree for NPU nodes (ARM-based systems)
    if [[ -d "/proc/device-tree" ]]; then
        npu_nodes=$(find /proc/device-tree -name "*npu*" -o -name "*neural*" 2>/dev/null)
        if [[ -n "$npu_nodes" ]]; then
            # Check for Rockchip specific nodes
            if echo "$npu_nodes" | grep -q "rknpu"; then
                if [[ -z "$NPU_VENDOR" ]]; then
                    NPU_VENDOR="Rockchip"
                    NPU_MODEL="Rockchip NPU"
                    NPU_COUNT=1
                    info "Rockchip NPU detected via device tree"
                fi
            elif [[ -z "$NPU_VENDOR" ]]; then
                NPU_VENDOR="Unknown"
                NPU_MODEL="Unknown NPU"
                NPU_COUNT=1
                info "NPU detected via device tree"
            fi
        fi
    fi
fi

# Final NPU validation and summary
if [[ -n "$NPU_VENDOR" && "$NPU_VENDOR" != "" ]]; then
    info "Final NPU detection result:"
    info "  Vendor(s): $NPU_VENDOR"
    if [[ -n "$NPU_MODEL" ]]; then
        info "  Model(s): $NPU_MODEL"
    fi
    info "  Count: $NPU_COUNT"
else
    info "No NPU detected"
    NPU_VENDOR=""
    NPU_MODEL=""
    NPU_COUNT=0
fi
# === /NPU ===

# Export variables for use by other scripts
export GPU_VENDOR
export GPU_MODEL
export GPU_COUNT