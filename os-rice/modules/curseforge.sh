# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/curseforge.sh — CurseForge (AUR). POSIX port of .../apps/curseforge.sh.
# CurseForge's PKGBUILD ships broken checksums, so it needs helper flags the
# generic aur: provider doesn't carry (--nosign + makepkg --skipchecksums) —
# hence a direct helper call rather than a pacman.map aur: row. Available module.
if pacman -Q curseforge >/dev/null 2>&1; then
    info "curseforge already installed (aur) - skipping"
else
    _cf_helper=$(_osr_aur_helper)
    [ -n "$_cf_helper" ] || error "no AUR helper (paru/yay) - install 'paru' before curseforge"
    warn "skipping checksum verification for curseforge (upstream ships broken sums)"
    run_step "Installing CurseForge (AUR)" as_user "$_cf_helper" -S --needed --noconfirm \
        --nosign curseforge --mflags "--skipchecksums"
fi
