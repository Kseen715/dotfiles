info "Installing Go..."

install_pkg_apt curl tar

# Map CPU_ARCH (set by detect-cpu.sh) to Go's download naming convention
if [ -z "$CPU_ARCH" ]; then
    error "CPU_ARCH is not set — ensure detect-cpu.sh ran before this module"
fi
case "$CPU_ARCH" in
    x86_64)          GO_ARCH="amd64"   ;;
    aarch64)         GO_ARCH="arm64"   ;;
    armv6l | armv7l) GO_ARCH="armv6l"  ;;
    i386   | i686)   GO_ARCH="386"     ;;
    *) error "Unsupported CPU architecture: $CPU_ARCH" ;;
esac
info "Detected Go architecture: $GO_ARCH"

# Fetch latest stable version from the official API (no jq required)
info "Fetching latest stable Go version..."
GO_VERSION=$(curl -f#SL "https://go.dev/dl/?mode=json" \
    | grep -o '"go[0-9][0-9]*\.[0-9][0-9]*\(\.[0-9][0-9]*\)\?"' \
    | head -1 \
    | tr -d '"')
if [ -z "$GO_VERSION" ]; then
    error "Failed to fetch latest Go version from go.dev"
fi
info "Latest stable Go version: $GO_VERSION"

# Check whether installation or upgrade is needed
GO_INSTALL_DIR="/usr/local/go"
NEED_INSTALL=false

if [ -x "$GO_INSTALL_DIR/bin/go" ]; then
    INSTALLED_VERSION="$("$GO_INSTALL_DIR/bin/go" version \
        | grep -o 'go[0-9][0-9]*\.[0-9][0-9]*\(\.[0-9][0-9]*\)\?' \
        | head -1)"
    info "Currently installed Go version: $INSTALLED_VERSION"
    if [ "$INSTALLED_VERSION" = "$GO_VERSION" ]; then
        info "Go $GO_VERSION is already up-to-date, skipping installation."
    else
        info "Upgrading Go: $INSTALLED_VERSION -> $GO_VERSION"
        NEED_INSTALL=true
    fi
else
    info "Go is not installed, proceeding with $GO_VERSION..."
    NEED_INSTALL=true
fi

if [ "$NEED_INSTALL" = true ]; then
    GO_TARBALL="${GO_VERSION}.linux-${GO_ARCH}.tar.gz"
    GO_DOWNLOAD_URL="https://go.dev/dl/${GO_TARBALL}"
    GO_TMP="/tmp/${GO_TARBALL}"

    info "Downloading $GO_DOWNLOAD_URL..."
    trace curl -f#SL -o "$GO_TMP" "$GO_DOWNLOAD_URL"
    check_error $? "Failed to download Go tarball from $GO_DOWNLOAD_URL"

    # go.dev explicitly requires removing the old tree before extracting
    trace rm -rf "$GO_INSTALL_DIR"
    check_error $? "Failed to remove old Go installation at $GO_INSTALL_DIR"

    trace tar -C /usr/local -xzf "$GO_TMP"
    check_error $? "Failed to extract Go tarball to /usr/local"

    trace rm -f "$GO_TMP"
    check_error $? "Failed to remove temporary tarball $GO_TMP"
fi

# Make Go available for the rest of this installation session
# (persistent PATH is managed via .zshrc in the dotfiles)
export PATH=$PATH:/usr/local/go/bin

# Verify
info "Verifying Go installation..."
trace /usr/local/go/bin/go version
check_error $? "Go installation verification failed"
