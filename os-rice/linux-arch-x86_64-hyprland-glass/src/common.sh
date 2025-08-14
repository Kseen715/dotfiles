# Grab --delevated <username> argument if provided
if [[ "$1" == "--delevated" && -n "$2" ]]; then
    DELEVATED_USER="$2"
    shift 2 # Remove the first two arguments
else
    DELEVATED_USER=""
fi

# Signal handler for Ctrl+C
cleanup() {
    echo ""
    error "Script interrupted by user (Ctrl+C). Exiting..."
}

# Trap SIGINT (Ctrl+C) and call cleanup function
trap cleanup SIGINT SIGTERM SIGQUIT

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Enhanced logging functions with colors
info() {
    printf "${CYAN}[INFO]${NC}\t%s\n" "$*"
}

warning() {
    printf "${YELLOW}[WARN]${NC}\t%s\n" "$*" >&2
}

error() {
    printf "${RED}[ERROR]${NC}\t%s\n" "$*" >&2
    exit 1
}

success() {
    printf "${GREEN}[DONE]${NC}\t%s\n" "$*"
}

trace() {
    printf "${NC}[BASH]${NC}\t%s\n" "$*"
    bash -c "$*"
    return $?
}

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
SCRIPT_DIR="${SCRIPT_DIR%/*}"

info "Checking if root..."
if [[ $EUID -ne 0 ]]; then
    # If not root, save username and re-execute with sudo
    USERNAME=$(whoami)
    info "Current user: $USERNAME"
    if ! command -v sudo &>/dev/null; then
        error "This script must be run as root. Use 'su' to switch to root user or install sudo"
    else
        info "Running with sudo..."
        # use absolute path to the script to avoid issues with relative paths
        if [[ ! -f "$SCRIPT_DIR/$(basename "$0")" ]]; then
            error "Script not found at expected location: $SCRIPT_DIR/$(basename "$0")"
        fi
        # Re-executes the script with sudo
        trace chmod +x "$SCRIPT_DIR/$(basename "$0")"
        trace exec sudo "$SCRIPT_DIR/$(basename "$0")" --delevated "$USERNAME" "$@"
        exit 0
    fi
else
    info "root - OK"
fi

TMP_FOLDER="/tmp/setup"
trace mkdir -p $TMP_FOLDER

# Function to install or update git repository
install_or_update_git_repo() {
    local repo_name="$1"
    local repo_url="$2"
    local repo_dir="$3"
    local clone_args="${4:-}"  # Optional additional arguments like --depth 1
    
    if [ -d "$repo_dir" ]; then
        info "$repo_name repository directory exists, checking repository..."
        local current_remote=$(git -C "$repo_dir" remote get-url origin 2>/dev/null || echo "")
        info "Current remote for $repo_name: $current_remote"
        if [ "$current_remote" = "$repo_url" ] || [ "$current_remote" = "$repo_url.git" ] || [ "$current_remote" = "${repo_url%.git}" ]; then
            info "Updating $repo_name repository..."
            if ! trace "git -C "$repo_dir" diff --quiet" || ! trace "git -C "$repo_dir" diff --cached --quiet"; then
                info "Changes detected in repository. Resetting to clean state..."
                trace git -C "$repo_dir" reset --hard HEAD && trace git -C "$repo_dir" clean -fd
            fi
            trace git -C "$repo_dir" pull
        else
            info "Different repository found for $repo_name, removing and cloning correct one..."
            trace rm -rf "$repo_dir"
            trace git clone $repo_url "$repo_dir" $clone_args
        fi
    else
        info "Installing $repo_name repository..."
        trace git clone $repo_url "$repo_dir" $clone_args
    fi
}

install_pkg_pacman() {
    local pkgs=("$@")
    local filtered_pkgs=()
    
    # Extract IgnorePkg entries from pacman.conf
    local ignore_list=""
    if [[ -f /etc/pacman.conf ]]; then
        # Get all IgnorePkg lines, remove comments, extract package names
        ignore_list=$(grep -E "^[[:space:]]*IgnorePkg[[:space:]]*=" /etc/pacman.conf | \
                     sed 's/^[[:space:]]*IgnorePkg[[:space:]]*=[[:space:]]*//' | \
                     tr ' ' '\n' | sort -u)
    fi
    
    # Filter out packages that are in the ignore list
    for pkg in "${pkgs[@]}"; do
        local skip=false
        if [[ -n "$ignore_list" ]]; then
            # Check if package is in ignore list (exact match or glob pattern)
            while IFS= read -r ignore_pkg; do
                [[ -z "$ignore_pkg" ]] && continue
                # Handle glob patterns in IgnorePkg
                if [[ "$pkg" == $ignore_pkg ]]; then
                    skip=true
                    break
                fi
            done <<< "$ignore_list"
        fi
        
        if [[ "$skip" == true ]]; then
            warning "$pkg is in the ignore list -- skipping"
        else
            filtered_pkgs+=("$pkg")
        fi
    done
    
    # Install filtered packages if any remain
    if [[ ${#filtered_pkgs[@]} -gt 0 ]]; then
        trace sudo pacman -S --needed --noconfirm "${filtered_pkgs[@]}"
    fi
}

install_pkg_aur() {
    local pkgs=("$@")
    local filtered_pkgs=()
    
    # Extract IgnorePkg entries from pacman.conf
    local ignore_list=""
    if [[ -f /etc/pacman.conf ]]; then
        # Get all IgnorePkg lines, remove comments, extract package names
        ignore_list=$(grep -E "^[[:space:]]*IgnorePkg[[:space:]]*=" /etc/pacman.conf | \
                     sed 's/^[[:space:]]*IgnorePkg[[:space:]]*=[[:space:]]*//' | \
                     tr ' ' '\n' | sort -u)
    fi
    
    # Filter out packages that are in the ignore list
    for pkg in "${pkgs[@]}"; do
        local skip=false
        if [[ -n "$ignore_list" ]]; then
            # Check if package is in ignore list (exact match or glob pattern)
            while IFS= read -r ignore_pkg; do
                [[ -z "$ignore_pkg" ]] && continue
                # Handle glob patterns in IgnorePkg
                if [[ "$pkg" == $ignore_pkg ]]; then
                    skip=true
                    break
                fi
            done <<< "$ignore_list"
        fi
        
        if [[ "$skip" == true ]]; then
            warning "$pkg is in the ignore list -- skipping"
        else
            filtered_pkgs+=("$pkg")
        fi
    done
    
    # Install filtered packages if any remain
    if [[ ${#filtered_pkgs[@]} -gt 0 ]]; then
        trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm "${filtered_pkgs[@]}"
    fi
}