# session: x11
# themable: yes
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
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
#   Qt5/6  qt5ct/qt6ct, both selected by QT_QPA_PLATFORMTHEME=qt5ct (the qt6ct
#          plugin registers that key too), exported from the xprofile layer for
#          X11 and from ~/.config/environment.d for a Wayland/systemd session
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

# The env var that selects qt5ct/qt6ct. The xprofile layer sets it too, but only
# an X11 session reads that file: GNOME/KDE on Wayland launch apps from the
# systemd user session, whose only env source is ~/.config/environment.d.
# Not theme-owned - one constant value, so it is a plain layer, not a template.
install_layer "$OSR_DOTFILES/environment.d/90-qt.conf" \
    "$OSR_HOME/.config/environment.d/90-qt.conf"

[ -n "${OSR_THEME:-}" ] || return 0

# --- GTK 2/3/4 and Qt5/6 ------------------------------------------------------
# One template per FILE SHAPE, not per theme (§6b): settings.ini is rendered
# twice because GTK3 and GTK4 read the same keys from two paths, and the Qt
# color scheme is rendered twice for the same reason. What used to be ten
# per-theme files is now six templates plus the theme's palette.
for _pair in \
    "gtk:settings.ini:$OSR_HOME/.config/gtk-3.0/settings.ini" \
    "gtk:settings.ini:$OSR_HOME/.config/gtk-4.0/settings.ini" \
    "gtk:gtk.css:$OSR_HOME/.config/gtk-3.0/gtk.css" \
    "gtk:gtk4.css:$OSR_HOME/.config/gtk-4.0/gtk.css" \
    "gtk:gtkrc-2.0:$OSR_HOME/.gtkrc-2.0" \
    "xsettingsd:xsettingsd.conf:$OSR_HOME/.config/xsettingsd/xsettingsd.conf" \
    "qtct:qt6ct.conf:$OSR_HOME/.config/qt6ct/qt6ct.conf" \
    "qtct:colors.conf:$OSR_HOME/.config/qt6ct/colors/rice.conf" \
    "qtct:qt5ct.conf:$OSR_HOME/.config/qt5ct/qt5ct.conf" \
    "qtct:colors.conf:$OSR_HOME/.config/qt5ct/colors/rice.conf" \
    ; do
    _th_app=${_pair%%:*}
    _th_rest=${_pair#*:}
    install_theme_layer "$_th_app" "${_th_rest%%:*}" "${_th_rest#*:}" || :
done

# GTK4/libadwaita reads gsettings, not settings.ini. The names come straight from
# theme.list rather than being parsed back out of the file this module just
# wrote - one source, no round trip. Best-effort: no dconf daemon in a container,
# and a failure here is cosmetic (§9).
if command -v gsettings >/dev/null 2>&1; then
    for _kv in "gtk-theme:gtk_theme" "icon-theme:icon_theme" \
               "cursor-theme:cursor_theme" "font-name:ui_font"; do
        _gs_v=$(osr_theme_meta "$OSR_THEME" "${_kv#*:}")
        [ -n "$_gs_v" ] || continue
        as_user gsettings set org.gnome.desktop.interface "${_kv%%:*}" "$_gs_v" 2>/dev/null || :
    done
    as_user gsettings set org.gnome.desktop.interface color-scheme \
        "prefer-$(osr_theme_meta "$OSR_THEME" polarity)" 2>/dev/null || :
    # GNOME 47+ tints its own shell chrome from a NAMED accent (there is no hex
    # key), so the theme names the nearest one. Older GNOME ignores the key.
    _gs_acc=$(osr_theme_meta "$OSR_THEME" gnome_accent)
    [ -n "$_gs_acc" ] && { as_user gsettings set org.gnome.desktop.interface \
        accent-color "$_gs_acc" 2>/dev/null || :; }
fi

# --- cursor theme: the root window needs telling separately -------------------
install_theme_layer icons default-index.theme \
    "$OSR_HOME/.local/share/icons/default/index.theme" || :

# --- ~/.Xresources: dotfiles base + rice palette (§5 by composition) ---------
# Xresources has no usable include for a per-user path, so the installed file is
# generated: the base (Xft rendering, DPI) followed by the rice's color block.
_xr_colors=$(osr_theme_source xresources colors) || _xr_colors=""
if [ -f "$OSR_DOTFILES/xresources/Xresources" ] && [ -n "$_xr_colors" ]; then
    _xr_tmp="${TMPDIR:-/tmp}/osr-xresources-$$"
    cat "$OSR_DOTFILES/xresources/Xresources" "$_xr_colors" > "$_xr_tmp"
    backup_copy "$_xr_tmp" "$OSR_HOME/.Xresources"
    rm -f "$_xr_tmp"
    case "$_xr_colors" in "${TMPDIR:-/tmp}"/osr-theme-*) rm -f "$_xr_colors" ;; esac
    [ -n "${DISPLAY:-}" ] && command -v xrdb >/dev/null 2>&1 \
        && as_user xrdb -merge "$OSR_HOME/.Xresources" 2>/dev/null || :
fi
