# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/pacman-multilib.sh — enable Arch's [multilib] repo (needed for 32-bit
# packages: steam, lib32-*). ONE copy, POSIX (was .../modules/pacman-multilib.sh).
# Arch-specific by nature; idempotent — the repo is added only once, then the
# index refreshed. No-op on non-pacman hosts (the rice is Arch-only anyway).
[ "$OSR_PKG" = pacman ] || { info "multilib is Arch-only - skipping"; return 0; }

if grep -q '^\[multilib\]' /etc/pacman.conf 2>/dev/null; then
    info "[multilib] already enabled in /etc/pacman.conf - skipping"
else
    run_step "Enabling [multilib] repository" as_root sh -c \
        'printf "\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n" >> /etc/pacman.conf'
    pkg_refresh
fi
