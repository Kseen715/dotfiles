# test/ref/hyprcursor_sh_ref.sh — the sh implementation of modules/hyprcursor.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/hyprcursor.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: wayland
# modules/hyprcursor.sh — hyprcursor + Bibata cursor theme (AUR). ONE copy, POSIX
# (was .../modules/hyprcursor.sh). The theme is copied into the user's icon dir;
# gsettings/flatpak overrides are best-effort (only when those tools exist).
run_step "Installing hyprcursor" pkg_install hyprcursor
run_step "Installing Bibata cursor theme (AUR)" pkg_install bibata-cursor-theme

as_user mkdir -p "$OSR_HOME/.local/share/icons"
if [ -d /usr/share/icons/Bibata-Modern-Ice ]; then
    as_user cp -rf /usr/share/icons/Bibata-Modern-Ice "$OSR_HOME/.local/share/icons/"
fi
if command -v gsettings >/dev/null 2>&1; then
    as_user gsettings set org.gnome.desktop.interface cursor-theme "Bibata-Modern-Ice" 2>/dev/null || true
    as_user gsettings set org.gnome.desktop.interface cursor-size 24 2>/dev/null || true
fi
if command -v flatpak >/dev/null 2>&1; then
    as_user flatpak override --user \
        --filesystem="$OSR_HOME/.themes:ro" \
        --filesystem="$OSR_HOME/.local/share/icons:ro" 2>/dev/null || true
fi
