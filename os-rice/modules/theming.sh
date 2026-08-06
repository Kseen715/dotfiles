# session: x11
# modules/theming.sh — toolkit theming for a WM that has no settings daemon
# (i3-sugg §4). Under GNOME/KDE something applies your theme; under i3 nothing
# does, which is why "the theme only works in some apps" is the single most
# common i3 complaint.
#
# Four consumers, four mechanisms, one rice:
#
#   GTK2   ~/.gtkrc-2.0
#   GTK3   ~/.config/gtk-3.0/settings.ini (+ gtk.css for accents)
#   GTK4   ~/.config/gtk-4.0/ + gsettings
#   Qt5/6  qt5ct/qt6ct, selected via QT_QPA_PLATFORMTHEME in the xprofile layer
#
# xsettingsd is the daemon that pushes theme/font/DPI to already-running GTK2/3
# apps (live-reload with `killall -HUP xsettingsd`). Everything below except the
# packages is rice-owned and swaps on a rice switch (§6).

run_step "Installing theming stack" pkg_install \
    xsettingsd lxappearance qt5ct qt6ct kvantum \
    gtk-dark-theme papirus-icon-theme adwaita-icon-theme xcursor-themes

run_step "Installing fonts" pkg_install \
    fontconfig noto-fonts noto-fonts-emoji noto-fonts-cjk \
    ttf-liberation ttf-dejavu nerd-fonts-symbols
run_step "Installing JetBrains Mono Nerd Font" osr_install_nerd_font JetBrainsMono

[ -n "${OSR_RICE_DIR:-}" ] || return 0
_rc="$OSR_RICE_DIR/config"

# --- GTK 2/3/4 ---------------------------------------------------------------
for _pair in \
    "gtk-3.0/settings.ini:$OSR_HOME/.config/gtk-3.0/settings.ini" \
    "gtk-3.0/gtk.css:$OSR_HOME/.config/gtk-3.0/gtk.css" \
    "gtk-4.0/settings.ini:$OSR_HOME/.config/gtk-4.0/settings.ini" \
    "gtk-4.0/gtk.css:$OSR_HOME/.config/gtk-4.0/gtk.css" \
    "gtk-2.0/gtkrc:$OSR_HOME/.gtkrc-2.0" \
    "xsettingsd/xsettingsd.conf:$OSR_HOME/.config/xsettingsd/xsettingsd.conf" \
    "qt6ct/qt6ct.conf:$OSR_HOME/.config/qt6ct/qt6ct.conf" \
    "qt6ct/colors.conf:$OSR_HOME/.config/qt6ct/colors/rice.conf" \
    "qt5ct/qt5ct.conf:$OSR_HOME/.config/qt5ct/qt5ct.conf" \
    "qt5ct/colors.conf:$OSR_HOME/.config/qt5ct/colors/rice.conf" \
    ; do
    _src="$_rc/${_pair%%:*}"
    [ -f "$_src" ] || continue
    install_layer "$_src" "${_pair#*:}"
done

# GTK4/libadwaita reads gsettings, not settings.ini. Best-effort: no dconf
# daemon in a container, and a failure here is cosmetic (§9).
if command -v gsettings >/dev/null 2>&1 && [ -f "$_rc/gtk-3.0/settings.ini" ]; then
    _gt=$(sed -n 's/^gtk-theme-name=//p'        "$_rc/gtk-3.0/settings.ini" | head -n1)
    _gi=$(sed -n 's/^gtk-icon-theme-name=//p'   "$_rc/gtk-3.0/settings.ini" | head -n1)
    _gc=$(sed -n 's/^gtk-cursor-theme-name=//p' "$_rc/gtk-3.0/settings.ini" | head -n1)
    _gf=$(sed -n 's/^gtk-font-name=//p'         "$_rc/gtk-3.0/settings.ini" | head -n1)
    for _kv in "gtk-theme:$_gt" "icon-theme:$_gi" "cursor-theme:$_gc" "font-name:$_gf"; do
        [ -n "${_kv#*:}" ] || continue
        as_user gsettings set org.gnome.desktop.interface "${_kv%%:*}" "${_kv#*:}" 2>/dev/null || :
    done
    as_user gsettings set org.gnome.desktop.interface color-scheme prefer-dark 2>/dev/null || :
fi

# --- cursor theme: the root window needs telling separately -------------------
if [ -f "$_rc/icons/default-index.theme" ]; then
    install_layer "$_rc/icons/default-index.theme" "$OSR_HOME/.local/share/icons/default/index.theme"
fi

# --- ~/.Xresources: dotfiles base + rice palette (§5 by composition) ---------
# Xresources has no usable include for a per-user path, so the installed file is
# generated: the base (Xft rendering, DPI) followed by the rice's color block.
if [ -f "$OSR_DOTFILES/xresources/Xresources" ] && [ -f "$_rc/Xresources/colors" ]; then
    _xr_tmp="${TMPDIR:-/tmp}/osr-xresources-$$"
    cat "$OSR_DOTFILES/xresources/Xresources" "$_rc/Xresources/colors" > "$_xr_tmp"
    backup_copy "$_xr_tmp" "$OSR_HOME/.Xresources"
    rm -f "$_xr_tmp"
    [ -n "${DISPLAY:-}" ] && command -v xrdb >/dev/null 2>&1 \
        && as_user xrdb -merge "$OSR_HOME/.Xresources" 2>/dev/null || :
fi
