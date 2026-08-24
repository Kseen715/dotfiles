# session: x11+wayland
# themable: yes
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/ghostty.sh — Ghostty terminal + JetBrains Mono Nerd Font + layered
# config. ONE copy, POSIX, distro-agnostic (was linux-debian/modules/ghostty.sh,
# a from-source Zig build). Native-first: native on arch/void and recent Ubuntu;
# elsewhere a community binary (Fedora COPR, ghostty-ubuntu .deb) and, as the
# last resort, built from source with a bootstrapped Zig toolchain
# (source:provide_ghostty via pkgmap). The source build is heavy (a full Zig
# compile) and is a real-desktop concern (§9), not container-tested.
#
# Config is split by ownership (§5), same shape as foot:
#
#   config          dotfiles-owned (10-layer) — overwritten on update; carries
#                   the ssh-comfort settings (terminfo, OSC 52 clipboard) and
#                   the 0.75 transparency
#   ghostty-theme   rice-owned palette (90-layer) — swapped on rice switch (§6),
#                   falling back to the dotfiles default when a rice ships none
#
# `config` ends with `config-file = ?ghostty-theme`, so the palette layer swaps
# independently of the base — the §5 split applied to a DE config. The '?' keeps
# a missing palette from being a startup error.
#
# The Nerd Font install is the shared, best-effort lib/fonts.sh helper (also used
# by foot/starship/wezterm) — one copy of the download-unzip-register logic.

run_step "Installing Ghostty" pkg_install ghostty unzip fontconfig
run_step "Installing JetBrains Mono Nerd Font" osr_install_nerd_font JetBrainsMono

# Base config (dotfiles-owned, overwrite-on-update §5).
if [ -f "$OSR_DOTFILES/ghostty/config" ]; then
    install_layer "$OSR_DOTFILES/ghostty/config" "$OSR_HOME/.config/ghostty/config"
fi

# Palette (rice-owned theme, swapped on switch §6). Rice override wins; the
# dotfiles default covers a rice that ships no palette. In --module mode
# OSR_THEME_DIR is whatever rice the theme picker resolved (§6).
if install_theme_layer ghostty ghostty-theme "$OSR_HOME/.config/ghostty/ghostty-theme"; then
    :
elif [ -f "$OSR_DOTFILES/ghostty/ghostty-theme" ]; then
    install_layer "$OSR_DOTFILES/ghostty/ghostty-theme" "$OSR_HOME/.config/ghostty/ghostty-theme"
fi

# --- WSLg: force the software GSK renderer ------------------------------------
# GTK draws Ghostty's window chrome through GSK, and under WSLg both accelerated
# GSK backends damage-track the titlebar wrongly: shrinking the centered title
# ("Ghostty" -> "~" at the first prompt) repaints only the new, narrower rect and
# leaves a sliver of the old glyph stranded to its left. Reproduced on
# GSK_RENDERER=gl and =vulkan; clean on =cairo, the software path.
#
# /etc/environment, because it is the only file BOTH launch paths read:
#
#   Windows Start  the WSLDVCPlugin shortcut is `wslg.exe -d <distro> --cd "~"
#                  -- /usr/bin/ghostty` — an absolute path with no shell and no
#                  desktop file in the loop. It also keeps only the first token
#                  of the entry's Exec=, so an `Exec=env VAR=x ghostty` override
#                  regenerates as a shortcut that launches /usr/bin/env.
#   linux cli      `ghostty`, which no desktop entry sees at all.
#
#   ruled out: ~/.config/environment.d (systemd user session, which a wslg launch
#   does not go through), a .desktop override (see above), a ~/.local/bin wrapper
#   (PATH is not consulted for the shortcut's absolute path).
#
# Distro-global rather than per-app, and that is the honest tradeoff: it puts
# every GTK4 app in the guest on the software renderer. Defensible here because
# what is broken is WSLg's GL stack, not Ghostty — the other GTK4 apps are
# drawing through the same one. Guarded by OSR_VIRT so a real desktop, where
# `gl` is both correct and cheaper, never sees it.
if [ "${OSR_VIRT:-none}" = wsl ]; then
    run_step "Ghostty: software GSK renderer (WSLg titlebar artifact)" \
        as_root sh -c '
            sed -i "/^GSK_RENDERER=/d" /etc/environment
            printf "GSK_RENDERER=cairo\n" >>/etc/environment
        '
fi
