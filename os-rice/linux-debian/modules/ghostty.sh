info "Installing Ghostty from source..."

# Build dependencies for Debian/Ubuntu
install_pkg_apt \
    libgtk-4-dev \
    libadwaita-1-dev \
    gettext \
    libxml2-utils \
    curl \
    tar \
    xz-utils \
    pkg-config

# gtk4-layer-shell is required but not packaged on many distros
# (e.g. Ubuntu 24.04, Debian 12). When absent, Ghostty compiles it from source.
GHOSTTY_EXTRA_FLAGS=""
if apt-cache show libgtk4-layer-shell-dev &>/dev/null 2>&1; then
    install_pkg_apt libgtk4-layer-shell-dev
else
    info "libgtk4-layer-shell-dev not available, using -fno-sys=gtk4-layer-shell"
    GHOSTTY_EXTRA_FLAGS="-fno-sys=gtk4-layer-shell"
fi

# Debian testing/unstable additionally requires gcc-multilib
OS_ID=$(. /etc/os-release && echo "$ID")
if [ "$OS_ID" = "debian" ] && [ -f /etc/debian_version ]; then
    if grep -qiE "sid|trixie" /etc/debian_version 2>/dev/null; then
        install_pkg_apt gcc-multilib
    fi
fi

# Fetch the latest stable Ghostty version from GitHub tags
# (ghostty-org/ghostty does not publish GitHub Releases, only tags)
info "Fetching latest Ghostty version..."
GHOSTTY_VERSION=$(curl -f#SL "https://api.github.com/repos/ghostty-org/ghostty/tags" \
    | grep '"name"' \
    | grep -v '"tip"' \
    | head -1 \
    | sed 's/.*"v\([^"]*\)".*/\1/')
if [ -z "$GHOSTTY_VERSION" ]; then
    error "Failed to fetch latest Ghostty version from GitHub API"
fi
info "Latest Ghostty version: $GHOSTTY_VERSION"

# Check if already installed and up-to-date
NEED_BUILD=true
if command -v ghostty &>/dev/null; then
    INSTALLED_VERSION=$(ghostty --version 2>/dev/null \
        | grep -o '[0-9]\+\.[0-9]\+\.[0-9]\+' | head -1)
    info "Currently installed Ghostty: $INSTALLED_VERSION"
    if [ "$INSTALLED_VERSION" = "$GHOSTTY_VERSION" ]; then
        info "Ghostty $GHOSTTY_VERSION is already up-to-date, skipping build."
        NEED_BUILD=false
    else
        info "Upgrading Ghostty: $INSTALLED_VERSION -> $GHOSTTY_VERSION"
    fi
fi

if [ "$NEED_BUILD" = true ]; then
    GHOSTTY_TARBALL="ghostty-${GHOSTTY_VERSION}.tar.gz"
    GHOSTTY_TMP="$TMP_FOLDER/$GHOSTTY_TARBALL"
    GHOSTTY_SRC="$TMP_FOLDER/ghostty-${GHOSTTY_VERSION}"

    info "Downloading Ghostty $GHOSTTY_VERSION source tarball..."
    trace curl -f#SL -o "$GHOSTTY_TMP" \
        "https://release.files.ghostty.org/${GHOSTTY_VERSION}/${GHOSTTY_TARBALL}"
    check_error $? "Failed to download Ghostty $GHOSTTY_VERSION source tarball"

    # Read the required Zig version from the tarball's .zig-version file
    ZIG_VERSION=$(tar -xOf "$GHOSTTY_TMP" \
        "ghostty-${GHOSTTY_VERSION}/.zig-version" 2>/dev/null | tr -d '[:space:]')
    info "Required Zig version from tarball: $ZIG_VERSION"

    # Fallback version table if .zig-version could not be extracted
    if [ -z "$ZIG_VERSION" ]; then
        warning "Could not read .zig-version from tarball, using built-in version table"
        case "$(echo "$GHOSTTY_VERSION" | cut -d. -f1,2)" in
            1.0 | 1.1) ZIG_VERSION="0.13.0" ;;
            1.2)       ZIG_VERSION="0.14.1" ;;
            1.3)       ZIG_VERSION="0.15.2" ;;
        esac
    fi
    info "Required Zig version: $ZIG_VERSION"

    if [ -z "$ZIG_VERSION" ]; then
        source "$(dirname "$(realpath "$0")")/modules/zig.sh"
        ZIG_BIN="zig"
    fi

    if [ "$ZIG_VERSION" ]; then
    # Map CPU_ARCH (from detect-cpu.sh) to Zig's download naming convention
    case "$CPU_ARCH" in
        x86_64)          ZIG_ARCH="x86_64"  ;;
        aarch64)         ZIG_ARCH="aarch64" ;;
        armv7l | armv6l) ZIG_ARCH="armv7a"  ;;
        i386   | i686)   ZIG_ARCH="x86"     ;;
        *) error "Unsupported architecture for Zig: $CPU_ARCH" ;;
    esac

    # Install Zig at the exact required version.
    # Kept in /usr/local/zig-VERSION so future Ghostty upgrades can reuse it
    # when the required Zig version has not changed.
        ZIG_DIR="/usr/local/zig-${ZIG_VERSION}"
        ZIG_BIN="$ZIG_DIR/zig"
        if [ -x "$ZIG_BIN" ] && "$ZIG_BIN" version 2>/dev/null | grep -q "^${ZIG_VERSION}$"; then
            info "Zig $ZIG_VERSION already present at $ZIG_DIR"
        else
            ZIG_TARBALL="zig-linux-${ZIG_ARCH}-${ZIG_VERSION}.tar.xz"
            ZIG_TMP="$TMP_FOLDER/$ZIG_TARBALL"

            info "Downloading Zig $ZIG_VERSION ($ZIG_ARCH)..."
            trace curl -f#SL -o "$ZIG_TMP" \
                "https://ziglang.org/download/${ZIG_VERSION}/${ZIG_TARBALL}"
            check_error $? "Failed to download Zig $ZIG_VERSION"

            trace mkdir -p "$ZIG_DIR"
            check_error $? "Failed to create Zig directory $ZIG_DIR"

            trace tar -xJf "$ZIG_TMP" -C "$ZIG_DIR" --strip-components=1
            check_error $? "Failed to extract Zig to $ZIG_DIR"

            trace rm -f "$ZIG_TMP"
            check_error $? "Failed to remove Zig tarball"

            info "Zig $ZIG_VERSION installed at $ZIG_DIR"
        fi
    fi

    # Extract Ghostty source
    info "Extracting Ghostty $GHOSTTY_VERSION source..."
    trace tar -xzf "$GHOSTTY_TMP" -C "$TMP_FOLDER"
    check_error $? "Failed to extract Ghostty source tarball"

    trace rm -f "$GHOSTTY_TMP"
    check_error $? "Failed to remove Ghostty source tarball"

    # Build and install Ghostty system-wide to /usr.
    # zig build must be run from inside the source directory.
    info "Building Ghostty $GHOSTTY_VERSION — this may take a few minutes..."
    trace bash -c "cd '$GHOSTTY_SRC' && '$ZIG_BIN' build -p /usr -Doptimize=ReleaseFast $GHOSTTY_EXTRA_FLAGS"
    check_error $? "Failed to build Ghostty"

    # Cleanup build directory
    trace rm -rf "$GHOSTTY_SRC"
    check_error $? "Failed to remove Ghostty build directory"

    info "Ghostty $GHOSTTY_VERSION installed to /usr"
fi
