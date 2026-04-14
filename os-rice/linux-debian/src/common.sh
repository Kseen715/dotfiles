# v0.1.2

# Grab --delevated <username> argument if provided
if [[ "$1" == "--delevated" && -n "$2" ]]; then
    DELEVATED_USER="$2"
    shift 2 # Remove the first two arguments
else
    DELEVATED_USER=""
fi

# When already running as root without --delevated (e.g. sudo ./install-module.sh),
# fall back to the invoking user via SUDO_USER.
if [[ $EUID -eq 0 && -z "$DELEVATED_USER" && -n "$SUDO_USER" && "$SUDO_USER" != "root" ]]; then
    DELEVATED_USER="$SUDO_USER"
fi

# Last resort: direct root shell (su -, root login, etc.) — install for root itself.
if [[ -z "$DELEVATED_USER" ]]; then
    DELEVATED_USER="root"
fi

# Resolve the real home directory for DELEVATED_USER.
# Using getent handles non-standard homes (e.g. /root for the root account).
DELEVATED_USER_HOME=$(getent passwd "$DELEVATED_USER" 2>/dev/null | cut -d: -f6)
DELEVATED_USER_HOME="${DELEVATED_USER_HOME:-/home/$DELEVATED_USER}"

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
    printf "${CYAN}%-8s${NC}%s\n" "[INFO]" "$*"
}

warning() {
    printf "${YELLOW}%-8s${NC}%s\n" "[WARN]" "$*" >&2
}

error() {
    printf "${RED}%-8s${NC}%s\n" "[ERROR]" "$*" >&2
    exit 1
}

success() {
    printf "${GREEN}%-8s${NC}%s\n" "[DONE]" "$*"
}

trace() {
    printf "${NC}%-8s${NC}%s\n" "[SHELL]" "$*"
    "$@"
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

# Download and install a GPG key for apt
# Usage: install_gpg_key_apt <key_url> <key_filename> [key_dir]
#   key_filename: bare filename, e.g. "myrepo.gpg" or "myrepo.asc"
#                 .gpg  -> key is dearmored (binary)
#                 .asc  -> key is saved as-is (ASCII armor, no dearmor)
#   key_dir:      directory to store the key (default: /etc/apt/trusted.gpg.d)
#                 use /etc/apt/keyrings for per-repo Signed-By references
install_gpg_key_apt() {
    local key_url="$1"
    local key_file="$2"
    local key_dir="${3:-/etc/apt/trusted.gpg.d}"
    local key_path="$key_dir/$key_file"

    if [ -z "$key_url" ] || [ -z "$key_file" ]; then
        error "install_gpg_key_apt: usage: install_gpg_key_apt <key_url> <key_filename> [key_dir]"
    fi

    if ! command -v gpg &>/dev/null; then
        install_pkg_apt gnupg
    fi

    trace install -m 0755 -d "$key_dir"
    check_error $? "Failed to create key directory $key_dir"

    if [ -f "$key_path" ]; then
        info "GPG key already present: $key_path -- skipping"
        return 0
    fi

    info "Adding GPG key from $key_url..."
    local tmp_asc="$TMP_FOLDER/$(basename "$key_url")"

    trace curl -f#SL -o "$tmp_asc" "$key_url"
    check_error $? "Failed to download GPG key from $key_url"

    if [[ "$key_file" == *.asc ]]; then
        # Keep ASCII armor as-is (used with DEB822 Signed-By field)
        trace mv "$tmp_asc" "$key_path"
        check_error $? "Failed to move GPG key to $key_path"
    else
        # Dearmor to binary .gpg for traditional trusted.gpg.d usage
        trace gpg --dearmor --yes -o "$key_path" "$tmp_asc"
        check_error $? "Failed to dearmor GPG key to $key_path"
        trace rm -f "$tmp_asc"
        check_error $? "Failed to remove temporary key file $tmp_asc"
    fi

    trace chmod 644 "$key_path"
    check_error $? "Failed to set permissions on $key_path"
}

# Add a Debian apt repository to sources.list.d
# Usage: install_deb_repo_apt <repo_url> <list_filename> [components]
#   list_filename: bare filename stored under /etc/apt/sources.list.d/
#                  e.g. "myrepo.list"
#   components:    defaults to "main"
install_deb_repo_apt() {
    local repo_url="$1"
    local list_file="$2"
    local components="${3:-main}"
    local list_path="/etc/apt/sources.list.d/$list_file"

    if [ -z "$repo_url" ] || [ -z "$list_file" ]; then
        error "install_deb_repo_apt: usage: install_deb_repo_apt <repo_url> <list_filename> [components]"
    fi

    # Detect distro codename
    local codename
    if command -v lsb_release &>/dev/null; then
        codename=$(lsb_release -sc 2>/dev/null)
    fi
    if [ -z "$codename" ] && [ -f /etc/os-release ]; then
        codename=$(. /etc/os-release && echo "$VERSION_CODENAME")
    fi
    if [ -z "$codename" ]; then
        error "install_deb_repo_apt: failed to detect distro codename for $repo_url"
    fi

    # Skip if the repo URL is already present in the file
    if [ -f "$list_path" ] && grep -qF "$repo_url" "$list_path" 2>/dev/null; then
        info "APT repository already present in $list_path -- skipping"
        return 0
    fi

    info "Adding APT repository: deb $repo_url $codename $components"

    # Write to a temp file first, then move atomically to avoid partial writes
    local tmp_list="$TMP_FOLDER/$(basename "$list_file")"
    echo "deb $repo_url $codename $components" > "$tmp_list"
    check_error $? "Failed to write temporary repo file $tmp_list"

    trace mv "$tmp_list" "$list_path"
    check_error $? "Failed to install repo file to $list_path"

    trace chmod 644 "$list_path"
    check_error $? "Failed to set permissions on $list_path"

    info "Updating package sources after adding $list_path..."
    trace apt update -q=2
    if [ $? -ne 0 ]; then
        warning "apt update failed, rolling back $list_path..."
        trace rm -f "$list_path"
        check_error $? "Failed to remove broken repo file $list_path during rollback"
        error "Rolled back $list_path — fix the repository URL or key and retry"
    fi
}

# Add a Debian apt repository in DEB822 format (.sources file)
# Usage: install_deb822_repo_apt <list_filename> <uri> <components> <key_path>
#   list_filename: bare filename stored under /etc/apt/sources.list.d/
#                  e.g. "docker.sources"
#   uri:           repository base URL
#   components:    e.g. "stable" or "main contrib"
#   key_path:      absolute path to the signing key set by install_gpg_key_apt
#                  e.g. "/etc/apt/keyrings/docker.asc"
install_deb822_repo_apt() {
    local list_file="$1"
    local uri="$2"
    local components="$3"
    local key_path="$4"
    local list_path="/etc/apt/sources.list.d/$list_file"

    if [ -z "$list_file" ] || [ -z "$uri" ] || [ -z "$components" ] || [ -z "$key_path" ]; then
        error "install_deb822_repo_apt: usage: install_deb822_repo_apt <list_filename> <uri> <components> <key_path>"
    fi

    # Detect distro codename
    local codename
    if command -v lsb_release &>/dev/null; then
        codename=$(lsb_release -sc 2>/dev/null)
    fi
    if [ -z "$codename" ] && [ -f /etc/os-release ]; then
        codename=$(. /etc/os-release && echo "$VERSION_CODENAME")
    fi
    if [ -z "$codename" ]; then
        error "install_deb822_repo_apt: failed to detect distro codename for $uri"
    fi

    # Detect architecture
    local arch
    if command -v dpkg &>/dev/null; then
        arch=$(dpkg --print-architecture 2>/dev/null)
    fi
    if [ -z "$arch" ]; then
        arch=$(uname -m)
    fi

    # Skip if URI is already present in the file
    if [ -f "$list_path" ] && grep -qF "$uri" "$list_path" 2>/dev/null; then
        info "DEB822 repository already present in $list_path -- skipping"
        return 0
    fi

    info "Adding DEB822 repository: $uri ($codename, $components) -> $list_path"

    # Write to temp file first, then move atomically
    local tmp_list="$TMP_FOLDER/$(basename "$list_file")"
    cat > "$tmp_list" << EOF
Types: deb
URIs: $uri
Suites: $codename
Components: $components
Architectures: $arch
Signed-By: $key_path
EOF
    check_error $? "Failed to write temporary DEB822 repo file $tmp_list"

    trace mv "$tmp_list" "$list_path"
    check_error $? "Failed to install DEB822 repo file to $list_path"

    trace chmod 644 "$list_path"
    check_error $? "Failed to set permissions on $list_path"

    info "Updating package sources after adding $list_path..."
    trace apt update -q=2
    if [ $? -ne 0 ]; then
        warning "apt update failed, rolling back $list_path..."
        trace rm -f "$list_path"
        check_error $? "Failed to remove broken repo file $list_path during rollback"
        error "Rolled back $list_path — fix the repository URL or key and retry"
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
            if ! trace git -C "$repo_dir" diff --quiet || ! trace git -C "$repo_dir" diff --cached --quiet; then
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
        trace sudo apt install --yes -q=2 "${filtered_pkgs[@]}"
        check_error $? "Failed to install packages: ${filtered_pkgs[*]}"
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
        check_error $? "Failed to install brew packages: ${filtered_pkgs[*]}"
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
    if [[ -d "$DELEVATED_USER_HOME/.cargo/bin" ]]; then
        # Get list of installed cargo binaries (this is approximate since cargo install doesn't track packages perfectly)
        installed_list=$(ls "$DELEVATED_USER_HOME/.cargo/bin" 2>/dev/null | sort -u)
    fi

    # Check for packages in a hypothetical ignore file (custom implementation)
    if [[ -f "$DELEVATED_USER_HOME/.cargo/ignore" ]]; then
        ignore_list=$(cat "$DELEVATED_USER_HOME/.cargo/ignore" | grep -v "^#" | grep -v "^$" | sort -u)
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
        trace sudo -u "$DELEVATED_USER" "$DELEVATED_USER_HOME/.cargo/bin/cargo" install "${filtered_pkgs[@]}" --locked
        check_error $? "Failed to install cargo packages: ${filtered_pkgs[*]}"
    fi
}
