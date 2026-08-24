# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/paru.sh — bootstrap the paru AUR helper. Listed first in an Arch rice
# so every later aur: package can dispatch through it (manifest order is the
# dependency graph, §4). paru resolves via pacman.map to source:provide_paru, so
# pkg_install builds it from the AUR once and skips on rerun (command -v probe).
# Arch-only; a no-op elsewhere (nothing maps `paru` on non-pacman hosts).
[ "$OSR_PKG" = pacman ] || { info "paru (AUR helper) is Arch-only - skipping"; return 0; }
run_step "Bootstrapping paru (AUR helper)" pkg_install paru
