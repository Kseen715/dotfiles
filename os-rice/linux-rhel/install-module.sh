#!/bin/bash

# v0.1.2

# Ask for sudo password once upfront (only if not already root) and keep the
# credential alive in a background loop for the lifetime of this process.
if [[ $EUID -ne 0 ]]; then
    sudo -v || exit 1
    (while true; do sudo -n true; sleep 60; kill -0 "$$" 2>/dev/null || exit; done) &
fi

source "$(dirname "$(realpath "$0")")/src/common.sh"

source "$(dirname "$(realpath "$0")")/src/repo-dotfiles.sh"

source "$(dirname "$(realpath "$0")")/src/detect-virt.sh"
source "$(dirname "$(realpath "$0")")/src/detect-cpu.sh"
source "$(dirname "$(realpath "$0")")/src/detect-hwaccel.sh"

inst_module() {
    local MODULE_NAME="$1"
    MODULE_PATH="$(dirname "$(realpath "$0")")/modules/$MODULE_NAME.sh"
    if [[ ! -f "$MODULE_PATH" ]]; then
        # check if module exists in apps folder
        MODULE_PATH="$(dirname "$(realpath "$0")")/apps/$MODULE_NAME.sh"
        if [[ ! -f "$MODULE_PATH" ]]; then
            error "Module $MODULE_NAME not found."
        fi
    fi
    info "Installing module \"$MODULE_NAME\"..."
    source "$MODULE_PATH"
    success "Module \"$MODULE_NAME\" installed successfully"
}

# read -m or --module <module_name> to run a specific module
if [[ -z "$1" ]]; then
    error "No module specified."
fi

MODULE_NAME="$1"
# check if module name is a list of modules, comma-separated
if [[ "$MODULE_NAME" == *","* ]]; then
    IFS=',' read -ra MODULES <<< "$MODULE_NAME"
    for MODULE in "${MODULES[@]}"; do
        inst_module "$MODULE"
    done
    exit 0
else
    inst_module "$MODULE_NAME"
fi
