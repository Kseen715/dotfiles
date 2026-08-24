# session: x11+wayland
# themable: yes
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/alacritty.sh — Alacritty terminal + JetBrains Mono Nerd Font + layered
# config. ONE copy, POSIX, distro-agnostic. Alacritty is native on every target
# (Debian/Ubuntu, Fedora, Arch, Void, Alpine, Gentoo), so the logical name passes
# through pkgmap unchanged - no source: row, no build.
#
# WHY IT IS HERE. It is the last actively-developed terminal that starts on
# pre-2011 Intel graphics: Ghostty requires OpenGL 4.3 (since 1.2) and kitty 3.3,
# while an Ironlake-era iGPU stops at desktop GL 2.1 and both die with "unable to
# acquire an OpenGL context for rendering". Alacritty carries a GLES2 fallback
# renderer and selects it automatically on that hardware - no env vars, and much
# faster than driving a GPU terminal through llvmpipe. On a modern GPU it takes
# the GL 3.3 path and behaves like any other install.
#
# Config is split by ownership (§5), same shape as foot/ghostty:
#
#   alacritty.toml         dotfiles-owned (10-layer) — overwritten on update;
#                          behaviour only, and the TERM=xterm-256color choice
#                          that keeps ssh from breaking (Alacritty has no
#                          ssh-terminfo equivalent)
#   alacritty-theme.toml   rice-owned palette (90-layer) — swapped on rice switch
#                          (§6), falling back to the dotfiles default when a rice
#                          ships none. Owns the colors AND window.opacity.
#
# alacritty.toml carries `import = ["~/.config/alacritty/alacritty-theme.toml"]`,
# so the palette layer swaps independently of the base — the §5 split applied to
# a DE config. install_alacritty_config, not install_layer: `import` moved into
# `[general]` in Alacritty 0.14, so the file is adapted to the Alacritty that was
# just installed (see lib/config.sh).
#
# Alacritty has no image protocol (no kitty graphics, no sixel - both refused
# upstream), so yazi previews images through chafa. modules/yazi.sh installs it;
# list `yazi` in the rice and image previews degrade to unicode blocks instead of
# vanishing (§9).
#
# The Nerd Font install is the shared, best-effort lib/fonts.sh helper (also used
# by foot/ghostty/starship/wezterm) — one copy of the download-unzip-register
# logic.

run_step "Installing Alacritty" pkg_install alacritty unzip fontconfig
run_step "Installing JetBrains Mono Nerd Font" osr_install_nerd_font JetBrainsMono

# Base config (dotfiles-owned, overwrite-on-update §5).
if [ -f "$OSR_DOTFILES/alacritty/alacritty.toml" ]; then
    install_alacritty_config "$OSR_DOTFILES/alacritty/alacritty.toml" \
        "$OSR_HOME/.config/alacritty/alacritty.toml"
fi

# Palette (rice-owned theme, swapped on switch §6). Rice override wins; the
# dotfiles default covers a rice that ships no palette. In --module mode
# OSR_THEME_DIR is whatever rice the theme picker resolved (§6).
if install_theme_layer alacritty alacritty-theme.toml \
    "$OSR_HOME/.config/alacritty/alacritty-theme.toml"; then
    :
elif [ -f "$OSR_DOTFILES/alacritty/alacritty-theme.toml" ]; then
    install_layer "$OSR_DOTFILES/alacritty/alacritty-theme.toml" \
        "$OSR_HOME/.config/alacritty/alacritty-theme.toml"
fi
