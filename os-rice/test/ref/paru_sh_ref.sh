# test/ref/paru_sh_ref.sh — the sh implementation of modules/paru.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/paru.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/paru.sh — bootstrap the paru AUR helper. Listed first in an Arch rice
# so every later aur: package can dispatch through it (manifest order is the
# dependency graph, §4). paru resolves via pacman.map to source:provide_paru, so
# pkg_install builds it from the AUR once and skips on rerun (command -v probe).
# Arch-only; a no-op elsewhere (nothing maps `paru` on non-pacman hosts).
[ "$OSR_PKG" = pacman ] || { info "paru (AUR helper) is Arch-only - skipping"; return 0; }
run_step "Bootstrapping paru (AUR helper)" pkg_install paru
