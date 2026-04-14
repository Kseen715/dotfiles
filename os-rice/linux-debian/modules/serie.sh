info "Installing serie..."
source "$(dirname "$(realpath "$0")")/modules/rust.sh"

install_pkg_cargo_locked serie
