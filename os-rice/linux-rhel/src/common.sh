# v0.1.3 2025-12-19

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
    printf "${NC}[SHELL]${NC}\t%s\n" "$*"
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

check_error() {
    local EXIT_CODE=$1
    local MSG="$2"
    if [ $EXIT_CODE -ne 0 ]; then
        error "$MSG (Exit code: $EXIT_CODE)"
    fi
}

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
            check_error $? "Failed to update $repo_name repository"
        else
            info "Different repository found for $repo_name, removing and cloning correct one..."
            trace rm -rf "$repo_dir"
            check_error $? "Failed to remove existing $repo_name repository"
            trace git clone $repo_url "$repo_dir" $clone_args
            check_error $? "Failed to clone $repo_name repository"
        fi
    else
        info "Installing $repo_name repository..."
        trace git clone $repo_url "$repo_dir" $clone_args
        check_error $? "Failed to clone $repo_name repository"
    fi
}

install_pkg_dnf() {
    local pkgs=("$@")
    local filtered_pkgs=()
    
    # Extract excluded packages from dnf configuration
    local ignore_list=""
    if command -v dnf &>/dev/null; then
        # Get list of excluded packages from dnf.conf
        ignore_list=$(dnf config-manager --dump 2>/dev/null | grep "^excludepkgs" | cut -d'=' -f2 | tr ',' '\n' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//' | sort -u)
    fi
    
    # Also check for packages marked as excluded in DNF config
    local excluded_list=""
    if [[ -f /etc/dnf/dnf.conf ]]; then
        excluded_list=$(awk -F'=' '/^excludepkgs/ {print $2}' /etc/dnf/dnf.conf | tr ',' '\n' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//' | sort -u)
    fi
    
    # Check for packages in /etc/dnf/vars/ or other plugin configs that might exclude packages
    local plugin_excludes=""
    if [[ -d /etc/dnf/plugins/versionlock.list ]]; then
        # DNF versionlock plugin - get locked packages
        plugin_excludes=$(cat /etc/dnf/plugins/versionlock.list 2>/dev/null | grep -v "^#" | grep -v "^$" | cut -d'-' -f1 | sort -u)
    fi
    
    # Combine all ignore lists
    local combined_ignore_list=""
    for list in "$ignore_list" "$excluded_list" "$plugin_excludes"; do
        if [[ -n "$list" ]]; then
            if [[ -n "$combined_ignore_list" ]]; then
                combined_ignore_list="$combined_ignore_list"$'\n'"$list"
            else
                combined_ignore_list="$list"
            fi
        fi
    done
    combined_ignore_list=$(echo "$combined_ignore_list" | sort -u | grep -v "^$")
    
    # Filter out packages that are in the ignore list
    for pkg in "${pkgs[@]}"; do
        local skip=false
        if [[ -n "$combined_ignore_list" ]]; then
            # Check if package is in ignore list (exact match or glob pattern)
            while IFS= read -r ignore_pkg; do
                [[ -z "$ignore_pkg" ]] && continue
                # Handle glob patterns
                if [[ "$pkg" == $ignore_pkg ]]; then
                    skip=true
                    break
                fi
            done <<< "$combined_ignore_list"
        fi
        
        if [[ "$skip" == true ]]; then
            warning "$pkg is excluded or locked -- skipping"
        else
            filtered_pkgs+=("$pkg")
        fi
    done
    
    # Install filtered packages if any remain
    if [[ ${#filtered_pkgs[@]} -gt 0 ]]; then
        trace sudo dnf install --assumeyes -q "${filtered_pkgs[@]}"
        check_error $? "Failed to install packages: ${filtered_pkgs[*]}"
    fi
}

install_pkg_cargo_locked() {
    local pkgs=("$@")
    local filtered_pkgs=()
    
    # Check for packages that might be restricted or problematic
    local ignore_list=""
    local restricted_list=""
    
    # Check if cargo is available
    if ! command -v cargo &>/dev/null; then
        error "cargo command not found. Please install Rust and Cargo first."
        return 1
    fi
    
    # Check for already installed packages (cargo doesn't have a "pinned" concept like other managers)
    # But we can check for packages that are already installed globally
    local installed_list=""
    if [[ -d "/home/$DELEVATED_USER/.cargo/bin" ]]; then
        # Get list of installed cargo binaries (this is approximate since cargo install doesn't track packages perfectly)
        installed_list=$(ls "/home/$DELEVATED_USER/.cargo/bin" 2>/dev/null | sort -u)
    fi
    
    # Check for packages in a hypothetical ignore file (custom implementation)
    if [[ -f "/home/$DELEVATED_USER/.cargo/ignore" ]]; then
        ignore_list=$(cat "/home/$DELEVATED_USER/.cargo/ignore" | grep -v "^#" | grep -v "^$" | sort -u)
    fi
    
    # Combine ignore and restricted lists
    local combined_ignore_list=""
    if [[ -n "$ignore_list" ]]; then
        combined_ignore_list="$ignore_list"
    fi
    if [[ -n "$restricted_list" ]]; then
        if [[ -n "$combined_ignore_list" ]]; then
            combined_ignore_list="$combined_ignore_list"$'\n'"$restricted_list"
        else
            combined_ignore_list="$restricted_list"
        fi
    fi
    
    # Filter out packages that are in the ignore list
    for pkg in "${pkgs[@]}"; do
        local skip=false
        
        # Check against ignore list
        if [[ -n "$combined_ignore_list" ]]; then
            while IFS= read -r ignore_pkg; do
                [[ -z "$ignore_pkg" ]] && continue
                # Handle exact match and simple glob patterns
                if [[ "$pkg" == $ignore_pkg ]] || [[ "$pkg" == ${ignore_pkg%\*}* ]]; then
                    skip=true
                    break
                fi
            done <<< "$combined_ignore_list"
        fi
        
        # Check if binary already exists (optional warning, not blocking)
        if [[ "$skip" == false ]] && [[ -n "$installed_list" ]]; then
            if echo "$installed_list" | grep -q "^${pkg}$"; then
                warning "$pkg binary already exists in ~/.cargo/bin (will be updated if different version)"
            fi
        fi
        
        if [[ "$skip" == true ]]; then
            warning "$pkg is in the ignore list -- skipping"
        else
            filtered_pkgs+=("$pkg")
        fi
    done
    
    # Install filtered packages if any remain
    if [[ ${#filtered_pkgs[@]} -gt 0 ]]; then
        trace sudo -u $DELEVATED_USER /home/$DELEVATED_USER/.cargo/bin/cargo install "${filtered_pkgs[@]}" --locked
        check_error $? "Failed to install cargo packages: ${filtered_pkgs[*]}"
    fi
}