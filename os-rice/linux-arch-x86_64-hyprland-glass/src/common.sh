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
