# session: x11
# modules/i3.sh — i3 window manager (X11) + layered config. ONE copy, POSIX.
# pacman ships it as i3-wm (pacman.map); Void and Debian call it i3.
#
# Config is split by ownership (§5), the same shape as ghostty/foot:
#
#   ~/.config/i3/config              dotfiles-owned (10-layer) — keybinds, rules,
#                                    autostart. Overwritten on update.
#   ~/.config/i3/config.d/90-theme.conf
#                                    rice-owned (90-layer) — colors, gaps, font,
#                                    bar. Swapped on rice switch (§6).
#   ~/.config/i3/config.d/99-local.conf
#                                    machine-owned, seeded empty, never touched.
#
# The base config ends with `include ~/.config/i3/config.d/*.conf`, so the theme
# layer swaps independently of the keybinds (i3 >= 4.20 has `include`, and it
# glob-expands the path).
#
# Companions installed here are the ones the shipped config actually invokes:
# i3status (fallback bar if polybar dies), dex (XDG autostart — i3 runs none of
# it by itself, §3.8), numlockx, autotiling (dwindle-style splits), unclutter
# (hide the pointer while typing), xclip (every screenshot/clipboard binding).

run_step "Installing i3" pkg_install i3 i3status dex numlockx autotiling unclutter xclip

_i3d="$OSR_HOME/.config/i3/config.d"
as_user mkdir -p "$_i3d"

# Base config (dotfiles-owned, overwrite-on-update §5).
if [ -f "$OSR_DOTFILES/i3/.config/i3/config" ]; then
    install_layer "$OSR_DOTFILES/i3/.config/i3/config" "$OSR_HOME/.config/i3/config"
fi

# Helper scripts the bindings call (power menu, layout toggle).
for _s in rofi-powermenu.sh; do
    if [ -f "$OSR_DOTFILES/i3/.config/i3/scripts/$_s" ]; then
        install_layer "$OSR_DOTFILES/i3/.config/i3/scripts/$_s" "$OSR_HOME/.config/i3/scripts/$_s"
        as_user chmod +x "$OSR_HOME/.config/i3/scripts/$_s"
    fi
done

# Theme layer (rice-owned, swapped on switch §6).
if [ -n "${OSR_THEME_DIR:-}" ] && [ -f "$OSR_THEME_DIR/config/i3/90-theme.conf" ]; then
    install_wallpaper_layer "$OSR_THEME_DIR/config/i3/90-theme.conf" "$_i3d/90-theme.conf"
fi

# Machine layer — yours, never rewritten.
seed_empty "$_i3d/99-local.conf"
