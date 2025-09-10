# v0.1.2

GPU_VENDOR=""
GPU_MODEL=""
GPU_COUNT=0

NPU_VENDOR=""
NPU_MODEL=""
NPU_COUNT=0

VPU_VENDOR=""
VPU_MODEL=""
VPU_COUNT=0

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
        *"Mali"*|*"mali"*|*"panfrost"*|*"ARM Mali"*|*"Immortalis"*|*"immortalis"*)
            echo "ARM"
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

# Try Mali GPU detection via dmesg
if [[ -z "$GPU_VENDOR" ]] && command -v dmesg &>/dev/null; then
    # info "Trying Mali GPU detection via dmesg..."
    mali_info=$(dmesg 2>/dev/null | grep -i -E "mali|panfrost.*gpu")
    if [[ -n "$mali_info" ]]; then
        # Look for Mali GPU model information
        mali_model=""
        
        # Check for panfrost driver with GPU model info
        panfrost_line=$(echo "$mali_info" | grep -i "panfrost.*gpu.*mali-g" | head -1)
        if [[ -n "$panfrost_line" ]]; then
            # Extract Mali model from panfrost line (e.g., "mali-g52 id 0x7402")
            mali_model=$(echo "$panfrost_line" | grep -o -i "mali-g[0-9]\+[a-z]*" | head -1)
            if [[ -n "$mali_model" ]]; then
                mali_model="ARM $(echo "$mali_model" | tr '[:lower:]' '[:upper:]')"
            fi
        fi
        
        # Fallback: check for generic mali driver
        if [[ -z "$mali_model" ]]; then
            mali_line=$(echo "$mali_info" | grep -i "mali.*gpu" | head -1)
            if [[ -n "$mali_line" ]]; then
                mali_model="ARM Mali GPU"
            fi
        fi
        
        if [[ -n "$mali_model" ]]; then
            GPU_VENDOR="ARM"
            GPU_MODEL="$mali_model"
            GPU_COUNT=1
            info "Detected Mali GPU via dmesg: $mali_model"
        fi
    fi
fi

# Try Mali GPU detection via device files
if [[ -z "$GPU_VENDOR" ]]; then
    # Check for Mali device files
    if [[ -c "/dev/mali0" ]] || [[ -d "/sys/class/misc/mali0" ]] || [[ -c "/dev/dri/renderD128" && -d "/sys/class/drm/card0" ]]; then
        # Check if it's actually a Mali GPU by looking at driver info
        if [[ -f "/sys/class/drm/card0/device/driver/module" ]]; then
            driver_module=$(readlink -f /sys/class/drm/card0/device/driver/module 2>/dev/null)
            if echo "$driver_module" | grep -q -i "mali\|panfrost"; then
                GPU_VENDOR="ARM"
                GPU_MODEL="ARM Mali GPU"
                GPU_COUNT=1
                info "Detected Mali GPU via device files"
            fi
        elif [[ -c "/dev/mali0" ]]; then
            GPU_VENDOR="ARM"
            GPU_MODEL="ARM Mali GPU"
            GPU_COUNT=1
            info "Detected Mali GPU via /dev/mali0 device"
        fi
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

# === VPU ===

# Detect VPU vendor and model
if command -v dmesg &>/dev/null; then
    # Get dmesg output and look for VPU-related entries
    dmesg_output=$(dmesg 2>/dev/null | grep -i -E "\bvpu\b|video.*processing|rockchip.*vpu\b|hantro|rkvdec|rkvenc|vdec|venc|cedrus.*vpu|allwinner.*vpu|mediatek.*vpu|imx.*vpu|amlogic.*vpu|meson.*vpu|venus.*vpu|qualcomm.*vpu")
    
    if [[ -n "$dmesg_output" ]]; then
        declare -A vpu_vendor_count
        declare -a detected_vpu_vendors
        declare -a detected_vpu_models
        declare -A unique_vpu_devices  # Track unique VPU devices by address/identifier
        
        # Process each VPU-related dmesg line
        while IFS= read -r line; do
            vpu_vendor=""
            vpu_model=""
            device_identifier=""
            
            # For Rockchip VPU, always use a consistent identifier regardless of device address
            if echo "$line" | grep -i -q "rockchip.*vpu\|hantro\|rkvdec\|rkvenc"; then
                # Create unique identifier based on VPU type
                if echo "$line" | grep -i -q "rkvdec"; then
                    device_identifier="rockchip_vdec_0"
                elif echo "$line" | grep -i -q "rkvenc"; then
                    device_identifier="rockchip_venc_0"
                elif echo "$line" | grep -i -q "hantro"; then
                    device_identifier="rockchip_hantro_0"
                else
                    device_identifier="rockchip_vpu_0"
                fi
            else
                # Extract device identifier (address) from dmesg line for other vendors
                device_addr=$(echo "$line" | grep -o "[0-9a-f]\{6,8\}\.vpu\|[0-9a-f]\{6,8\}\.video")
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
            
            # Rockchip VPU detection
            if echo "$line" | grep -i -q "rockchip.*vpu\|hantro\|rkvdec\|rkvenc"; then
                vpu_vendor="Rockchip"
                # Universal method: extract VPU model from system name (2nd word)
                detect_system_name_inxi
                if [[ -n "$SYS_NAME" ]]; then
                    # Extract the second word from system name (should be the SoC model)
                    soc_model=$(echo "$SYS_NAME" | awk '{print $2}')
                    if [[ -n "$soc_model" && "$soc_model" =~ ^RK[0-9] ]]; then
                        if echo "$line" | grep -i -q "rkvdec"; then
                            vpu_model="$(echo "$soc_model" | tr '[:lower:]' '[:upper:]') VDEC"
                        elif echo "$line" | grep -i -q "rkvenc"; then
                            vpu_model="$(echo "$soc_model" | tr '[:lower:]' '[:upper:]') VENC"
                        elif echo "$line" | grep -i -q "hantro"; then
                            vpu_model="$(echo "$soc_model" | tr '[:lower:]' '[:upper:]') Hantro VPU"
                        else
                            vpu_model="$(echo "$soc_model" | tr '[:lower:]' '[:upper:]') VPU"
                        fi
                    else
                        # Fallback: extract from dmesg line
                        model_match=$(echo "$line" | grep -o -i "rk[0-9]\+[a-z]*")
                        if [[ -n "$model_match" ]]; then
                            if echo "$line" | grep -i -q "rkvdec"; then
                                vpu_model="$(echo "$model_match" | tr '[:lower:]' '[:upper:]') VDEC"
                            elif echo "$line" | grep -i -q "rkvenc"; then
                                vpu_model="$(echo "$model_match" | tr '[:lower:]' '[:upper:]') VENC"
                            elif echo "$line" | grep -i -q "hantro"; then
                                vpu_model="$(echo "$model_match" | tr '[:lower:]' '[:upper:]') Hantro VPU"
                            else
                                vpu_model="$(echo "$model_match" | tr '[:lower:]' '[:upper:]') VPU"
                            fi
                        else
                            if echo "$line" | grep -i -q "rkvdec"; then
                                vpu_model="Rockchip VDEC"
                            elif echo "$line" | grep -i -q "rkvenc"; then
                                vpu_model="Rockchip VENC"
                            elif echo "$line" | grep -i -q "hantro"; then
                                vpu_model="Rockchip Hantro VPU"
                            else
                                vpu_model="Rockchip VPU"
                            fi
                        fi
                    fi
                else
                    # Fallback: extract from dmesg line
                    model_match=$(echo "$line" | grep -o -i "rk[0-9]\+[a-z]*")
                    if [[ -n "$model_match" ]]; then
                        if echo "$line" | grep -i -q "rkvdec"; then
                            vpu_model="$(echo "$model_match" | tr '[:lower:]' '[:upper:]') VDEC"
                        elif echo "$line" | grep -i -q "rkvenc"; then
                            vpu_model="$(echo "$model_match" | tr '[:lower:]' '[:upper:]') VENC"
                        elif echo "$line" | grep -i -q "hantro"; then
                            vpu_model="$(echo "$model_match" | tr '[:lower:]' '[:upper:]') Hantro VPU"
                        else
                            vpu_model="$(echo "$model_match" | tr '[:lower:]' '[:upper:]') VPU"
                        fi
                    else
                        if echo "$line" | grep -i -q "rkvdec"; then
                            vpu_model="Rockchip VDEC"
                        elif echo "$line" | grep -i -q "rkvenc"; then
                            vpu_model="Rockchip VENC"
                        elif echo "$line" | grep -i -q "hantro"; then
                            vpu_model="Rockchip Hantro VPU"
                        else
                            vpu_model="Rockchip VPU"
                        fi
                    fi
                fi
            # Allwinner VPU detection (Cedrus)
            elif echo "$line" | grep -i -q "cedrus.*vpu\|allwinner.*vpu"; then
                vpu_vendor="Allwinner"
                if echo "$line" | grep -i -q "cedrus"; then
                    vpu_model="Allwinner Cedrus VPU"
                else
                    vpu_model="Allwinner VPU"
                fi
            # Qualcomm VPU detection (Venus)
            elif echo "$line" | grep -i -q "qualcomm.*vpu\|venus.*vpu"; then
                vpu_vendor="Qualcomm"
                if echo "$line" | grep -i -q "venus"; then
                    vpu_model="Qualcomm Venus VPU"
                else
                    vpu_model="Qualcomm VPU"
                fi
            # MediaTek VPU detection
            elif echo "$line" | grep -i -q "mediatek.*vpu"; then
                vpu_vendor="MediaTek"
                vpu_model="MediaTek VPU"
            # Amlogic VPU detection (Meson)
            elif echo "$line" | grep -i -q "amlogic.*vpu\|meson.*vpu"; then
                vpu_vendor="Amlogic"
                if echo "$line" | grep -i -q "meson"; then
                    vpu_model="Amlogic Meson VPU"
                else
                    vpu_model="Amlogic VPU"
                fi
            # NXP i.MX VPU detection
            elif echo "$line" | grep -i -q "imx.*vpu"; then
                vpu_vendor="NXP"
                vpu_model="NXP i.MX VPU"
            # Generic video processing/vpu detection
            elif echo "$line" | grep -i -q "video.*processing\|\bvpu\b"; then
                vpu_vendor="Unknown"
                vpu_model="Unknown VPU"
            fi
            
            if [[ -n "$vpu_vendor" && -n "$device_identifier" ]]; then
                # Only count each unique device once
                device_key="${vpu_vendor}_${device_identifier}"
                if [[ -z "${unique_vpu_devices[$device_key]}" ]]; then
                    unique_vpu_devices[$device_key]=1
                    
                    # Count occurrences of each vendor
                    if [[ -z "${vpu_vendor_count[$vpu_vendor]}" ]]; then
                        vpu_vendor_count[$vpu_vendor]=1
                        detected_vpu_vendors+=("$vpu_vendor")
                    else
                        ((vpu_vendor_count[$vpu_vendor]++))
                    fi
                    
                    # Store full model information (only once per unique device)
                    if [[ -n "$vpu_model" ]]; then
                        detected_vpu_models+=("$vpu_model")
                    fi
                    
                    ((VPU_COUNT++))
                fi
            fi
        done <<< "$dmesg_output"
        
        # Create comma-separated vendor list
        if [[ ${#detected_vpu_vendors[@]} -gt 0 ]]; then
            VPU_VENDOR=$(IFS=","; echo "${detected_vpu_vendors[*]}")
            VPU_MODEL=$(IFS=","; echo "${detected_vpu_models[*]}")
            
            info "Detected $VPU_COUNT VPU(s) via dmesg:"
            for vendor in "${detected_vpu_vendors[@]}"; do
                count=${vpu_vendor_count[$vendor]}
                if [[ $count -eq 1 ]]; then
                    info "  - $vendor VPU"
                else
                    info "  - $vendor VPU (x$count)"
                fi
            done
            
            if [[ -n "$VPU_MODEL" ]]; then
                info "VPU model(s): $VPU_MODEL"
            fi
        fi
    fi
else
    warning "dmesg command not found, unable to detect VPU via kernel messages"
fi

# Additional VPU detection methods
# Check for VPU-specific device files and drivers
if [[ -z "$VPU_VENDOR" ]]; then
    # info "Trying additional VPU detection methods..."
    
    # Check for Rockchip VPU device files
    if [[ -c "/dev/rkvdec" ]] || [[ -c "/dev/rkvenc" ]] || [[ -d "/sys/class/video4linux" ]]; then
        # Check if it's actually a Rockchip VPU by looking at driver info
        vpu_found=0
        for v4l_device in /sys/class/video4linux/video*; do
            if [[ -d "$v4l_device" ]]; then
                device_name=$(cat "$v4l_device/name" 2>/dev/null)
                if echo "$device_name" | grep -i -q "rockchip\|rkvdec\|rkvenc\|hantro"; then
                    if [[ -z "$VPU_VENDOR" ]]; then
                        VPU_VENDOR="Rockchip"
                        if echo "$device_name" | grep -i -q "rkvdec"; then
                            VPU_MODEL="Rockchip VDEC"
                        elif echo "$device_name" | grep -i -q "rkvenc"; then
                            VPU_MODEL="Rockchip VENC"
                        elif echo "$device_name" | grep -i -q "hantro"; then
                            VPU_MODEL="Rockchip Hantro VPU"
                        else
                            VPU_MODEL="Rockchip VPU"
                        fi
                        VPU_COUNT=1
                        vpu_found=1
                        info "Rockchip VPU detected via V4L2 device: $device_name"
                        break
                    fi
                fi
            fi
        done
        
        # Fallback: check for rkvdec/rkvenc device files directly
        if [[ $vpu_found -eq 0 ]]; then
            if [[ -c "/dev/rkvdec" ]] || [[ -c "/dev/rkvenc" ]]; then
                VPU_VENDOR="Rockchip"
                if [[ -c "/dev/rkvdec" && -c "/dev/rkvenc" ]]; then
                    VPU_MODEL="Rockchip VDEC/VENC"
                elif [[ -c "/dev/rkvdec" ]]; then
                    VPU_MODEL="Rockchip VDEC"
                elif [[ -c "/dev/rkvenc" ]]; then
                    VPU_MODEL="Rockchip VENC"
                fi
                VPU_COUNT=1
                info "Rockchip VPU detected via device files"
            fi
        fi
    fi
    
    # Check for loaded VPU kernel modules
    if command -v lsmod &>/dev/null; then
        vpu_modules=$(lsmod | grep -i -E "rkvdec|rkvenc|hantro|cedrus|venus|vpu|video.*codec")
        if [[ -n "$vpu_modules" ]]; then
            while IFS= read -r module_line; do
                module_name=$(echo "$module_line" | awk '{print $1}')
                case "$module_name" in
                    *rkvdec*|*rkvenc*|*hantro*)
                        if [[ -z "$VPU_VENDOR" ]]; then
                            VPU_VENDOR="Rockchip"
                            if echo "$module_name" | grep -q "rkvdec"; then
                                VPU_MODEL="Rockchip VDEC"
                            elif echo "$module_name" | grep -q "rkvenc"; then
                                VPU_MODEL="Rockchip VENC"
                            elif echo "$module_name" | grep -q "hantro"; then
                                VPU_MODEL="Rockchip Hantro VPU"
                            else
                                VPU_MODEL="Rockchip VPU"
                            fi
                            VPU_COUNT=1
                            info "Rockchip VPU detected via kernel module: $module_name"
                        fi
                        ;;
                    *cedrus*)
                        if [[ -z "$VPU_VENDOR" ]]; then
                            VPU_VENDOR="Allwinner"
                            VPU_MODEL="Allwinner Cedrus VPU"
                            VPU_COUNT=1
                            info "Allwinner VPU detected via kernel module: $module_name"
                        fi
                        ;;
                    *venus*)
                        if [[ -z "$VPU_VENDOR" ]]; then
                            VPU_VENDOR="Qualcomm"
                            VPU_MODEL="Qualcomm Venus VPU"
                            VPU_COUNT=1
                            info "Qualcomm VPU detected via kernel module: $module_name"
                        fi
                        ;;
                    *vpu*|*video*codec*)
                        if [[ -z "$VPU_VENDOR" ]]; then
                            VPU_VENDOR="Unknown"
                            VPU_MODEL="Unknown VPU"
                            VPU_COUNT=1
                            info "VPU detected via kernel module: $module_name"
                        fi
                        ;;
                esac
            done <<< "$vpu_modules"
        fi
    fi
    
    # Check /proc/device-tree for VPU nodes (ARM-based systems)
    if [[ -d "/proc/device-tree" ]]; then
        vpu_nodes=$(find /proc/device-tree -name "*vpu*" -o -name "*vdec*" -o -name "*venc*" -o -name "*video*codec*" 2>/dev/null)
        if [[ -n "$vpu_nodes" ]]; then
            # Check for Rockchip specific nodes
            if echo "$vpu_nodes" | grep -i -q "rkvdec\|rkvenc\|rockchip.*vpu"; then
                if [[ -z "$VPU_VENDOR" ]]; then
                    VPU_VENDOR="Rockchip"
                    VPU_MODEL="Rockchip VPU"
                    VPU_COUNT=1
                    info "Rockchip VPU detected via device tree"
                fi
            # Check for Allwinner/Cedrus nodes
            elif echo "$vpu_nodes" | grep -i -q "cedrus\|allwinner.*vpu"; then
                if [[ -z "$VPU_VENDOR" ]]; then
                    VPU_VENDOR="Allwinner"
                    VPU_MODEL="Allwinner Cedrus VPU"
                    VPU_COUNT=1
                    info "Allwinner VPU detected via device tree"
                fi
            elif [[ -z "$VPU_VENDOR" ]]; then
                VPU_VENDOR="Unknown"
                VPU_MODEL="Unknown VPU"
                VPU_COUNT=1
                info "VPU detected via device tree"
            fi
        fi
    fi
fi

# Final VPU validation and summary
if [[ -n "$VPU_VENDOR" && "$VPU_VENDOR" != "" ]]; then
    info "Final VPU detection result:"
    info "  Vendor(s): $VPU_VENDOR"
    if [[ -n "$VPU_MODEL" ]]; then
        info "  Model(s): $VPU_MODEL"
    fi
    info "  Count: $VPU_COUNT"
else
    info "No VPU detected"
    VPU_VENDOR=""
    VPU_MODEL=""
    VPU_COUNT=0
fi

# === /VPU ===