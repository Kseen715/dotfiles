info "Installing Zig..."
install_gpg_key_apt \
    "https://debian.griffo.io/EA0F721D231FDD3A0A17B9AC7808B4DD62C41256.asc" \
    "debian.griffo.io.gpg"

install_deb_repo_apt \
    "https://debian.griffo.io/apt" \
    "debian.griffo.io.list"

install_pkg_apt \
    zig
