# v0.1.2

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

install_pkg_apt() {
    local pkgs=("$@")
    local filtered_pkgs=()
    
    # Extract held packages from dpkg status
    local ignore_list=""
    if command -v dpkg &>/dev/null; then
        # Get list of held packages
        ignore_list=$(dpkg --get-selections | grep hold | cut -f1 | sort -u)
    fi
    
    # Also check for packages in apt preferences (pinned packages)
    local pinned_list=""
    if [[ -f /etc/apt/preferences ]] || [[ -d /etc/apt/preferences.d ]]; then
        # Get packages with negative priority (effectively ignored)
        if [[ -f /etc/apt/preferences ]]; then
            pinned_list+=$(awk '/^Package:/ {pkg=$2} /^Pin-Priority:/ {if ($2 < 0) print pkg}' /etc/apt/preferences 2>/dev/null | sort -u)
        fi
        if [[ -d /etc/apt/preferences.d ]]; then
            for pref_file in /etc/apt/preferences.d/*; do
                [[ -f "$pref_file" ]] || continue
                pinned_list+=$'\n'$(awk '/^Package:/ {pkg=$2} /^Pin-Priority:/ {if ($2 < 0) print pkg}' "$pref_file" 2>/dev/null | sort -u)
            done
        fi
        pinned_list=$(echo "$pinned_list" | sort -u | grep -v "^$")
    fi
    
    # Combine ignore and pinned lists
    local combined_ignore_list=""
    if [[ -n "$ignore_list" ]]; then
        combined_ignore_list="$ignore_list"
    fi
    if [[ -n "$pinned_list" ]]; then
        if [[ -n "$combined_ignore_list" ]]; then
            combined_ignore_list="$combined_ignore_list"$'\n'"$pinned_list"
        else
            combined_ignore_list="$pinned_list"
        fi
    fi
    
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
            warning "$pkg is held or pinned -- skipping"
        else
            filtered_pkgs+=("$pkg")
        fi
    done
    
    # Install filtered packages if any remain
    if [[ ${#filtered_pkgs[@]} -gt 0 ]]; then
        trace sudo apt install -y "${filtered_pkgs[@]}"
    fi
}

install_pkg_brew() {
    local pkgs=("$@")
    local filtered_pkgs=()
    
    # Extract pinned packages from Homebrew
    local ignore_list=""
    if command -v brew &>/dev/null; then
        # Get list of pinned packages
        ignore_list=$(brew list --pinned 2>/dev/null | sort -u)
    fi
    
    # Also check for packages in HOMEBREW_NO_INSTALL_FROM_API or similar restrictions
    local restricted_list=""
    if [[ -n "$HOMEBREW_NO_INSTALL_FROM_API" ]]; then
        # If API installation is disabled, we might want to be more cautious
        # This is just a placeholder for potential future restrictions
        restricted_list=""
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
        if [[ -n "$combined_ignore_list" ]]; then
            # Check if package is in ignore list (exact match)
            while IFS= read -r ignore_pkg; do
                [[ -z "$ignore_pkg" ]] && continue
                # Homebrew package names are typically exact matches
                if [[ "$pkg" == "$ignore_pkg" ]]; then
                    skip=true
                    break
                fi
            done <<< "$combined_ignore_list"
        fi
        
        # Also check if package is already installed and pinned
        if [[ "$skip" == false ]] && command -v brew &>/dev/null; then
            if brew list --pinned "$pkg" &>/dev/null; then
                skip=true
            fi
        fi
        
        if [[ "$skip" == true ]]; then
            warning "$pkg is pinned -- skipping"
        else
            filtered_pkgs+=("$pkg")
        fi
    done
    
    # Install filtered packages if any remain
    if [[ ${#filtered_pkgs[@]} -gt 0 ]]; then
        trace brew install "${filtered_pkgs[@]}"
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
    if [[ -d "$HOME/.cargo/bin" ]]; then
        # Get list of installed cargo binaries (this is approximate since cargo install doesn't track packages perfectly)
        installed_list=$(ls "$HOME/.cargo/bin" 2>/dev/null | sort -u)
    fi
    
    # Check for packages in a hypothetical ignore file (custom implementation)
    if [[ -f "$HOME/.cargo/ignore" ]]; then
        ignore_list=$(cat "$HOME/.cargo/ignore" | grep -v "^#" | grep -v "^$" | sort -u)
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
        trace cargo install "${filtered_pkgs[@]}" --locked
    fi
}