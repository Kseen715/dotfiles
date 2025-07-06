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
        if [[ ! -f "$SCRIPT_DIR/../$(basename "$0")" ]]; then
            error "Script not found at expected location: $SCRIPT_DIR/../$(basename "$0")"
        fi
        # Re-executes the script with sudo
        trace chmod +x "$SCRIPT_DIR/../$(basename "$0")"
        trace exec sudo "$SCRIPT_DIR/../$(basename "$0")" --delevated "$USERNAME" "$@"
        exit 0
    fi
else
    info "root - OK"
fi

TMP_FOLDER="/tmp/setup"
trace mkdir -p $TMP_FOLDER