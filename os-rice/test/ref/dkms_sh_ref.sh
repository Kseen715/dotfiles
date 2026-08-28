# test/ref/dkms_sh_ref.sh — the sh implementation of modules/dkms.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/dkms.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/dkms.sh — DKMS + kernel headers for every installed kernel flavor. ONE
# copy, POSIX (was .../modules/dkms.sh). Headers must match the *running* kernel,
# so this is validated on hardware, not in CI (§9). Arch-only (kernel package
# names are pacman's); no-op elsewhere.
[ "$OSR_PKG" = pacman ] || { info "dkms module is Arch-specific - skipping"; return 0; }
run_step "Installing DKMS" pkg_install dkms
for _k in linux linux-lts linux-zen; do
    if pacman -Qq "$_k" >/dev/null 2>&1; then
        run_step "Installing $_k headers" pkg_install "$_k-headers"
    fi
done
