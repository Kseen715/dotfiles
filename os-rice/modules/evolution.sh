# session: x11+wayland
# modules/evolution.sh — Evolution mail/calendar/contacts, made to look like a
# 2020s client instead of a 2009 one.
#
# Evolution's appearance comes from three places, and only one of them is a
# config file, which is why this module is longer than "install a package":
#
#   1. GSettings      layout, density, whether HTML mail is allowed to paint its
#                     own white background over your dark theme
#   2. a scoped GTK   Evolution is GTK3 and reads the global gtk.css, so the
#      theme          rice's tweaks are shipped as a *private theme* the .desktop
#                     override selects with GTK_THEME. That keeps the rounded
#                     header-bar look off every other GTK app.
#   3. fonts          the message body is WebKit; it uses its own font settings
#                     unless you tell it to follow the desktop
#
# GSettings keys come and go between Evolution releases, so each one is applied
# only if the installed schema actually has it (`gsettings list-keys`). A key
# this version does not know is a skipped line, not a failed module (§9).
#
# evolution-ews is the Exchange/Office365 backend — the single most common reason
# an account cannot be added at all, and it is a separate package everywhere.
#
# The two support packages are what §1 below actually runs on, and they are the
# reason this list is not just "evolution":
#
#   dconf   the GSettings *backend*. Without it GSettings falls back to the
#           memory backend and every `gsettings set` is discarded at logout.
#   glib2   ships the `gsettings` binary itself. Arch/Fedora call it glib2, Void
#           and Alpine glib, Debian/Ubuntu split it out as libglib2.0-bin — and
#           Debian has no `dconf` binary package at all, only dconf-cli +
#           dconf-gsettings-backend. All of that is absorbed by pkgmap rows (§1),
#           so this list stays one list.

run_step "Installing Evolution" pkg_install \
    evolution evolution-data-server evolution-ews \
    dconf glib2 gsettings-desktop-schemas

# --- 1. GSettings -------------------------------------------------------------
#
# osr_gsettings_apply <file> — apply `<schema> <key> <value>` lines as OSR_USER,
# skipping keys the installed version does not have. Comments and blanks ignored.
osr_gsettings_apply() {
    _gs_file=$1
    [ -f "$_gs_file" ] || return 0
    if ! command -v gsettings >/dev/null 2>&1; then
        warn "gsettings not available - skipping $(basename "$_gs_file")"
        return 0
    fi
    _gs_set=0
    _gs_skip=0
    while IFS= read -r _gs_line || [ -n "$_gs_line" ]; do
        # Comment handling has to survive hex colors: in `citation-color
        # '#d98cae'` the # is a value, not a comment. So: trim, then drop only a
        # comment that is preceded by whitespace (the same rule _pkgmap_one uses
        # in lib/pkg.sh), then skip whole-line comments by their first character.
        _gs_line=$(printf '%s' "$_gs_line" | sed 's/^[[:space:]]*//; s/[[:space:]]#.*$//; s/[[:space:]]*$//')
        case "$_gs_line" in ''|'#'*) continue ;; esac
        _gs_schema=${_gs_line%% *}
        _gs_rest=${_gs_line#"$_gs_schema" }
        _gs_key=${_gs_rest%% *}
        _gs_val=${_gs_rest#"$_gs_key" }
        [ -n "$_gs_schema" ] && [ -n "$_gs_key" ] && [ -n "$_gs_val" ] || continue
        if as_user gsettings list-keys "$_gs_schema" 2>/dev/null | grep -qx "$_gs_key"; then
            if as_user gsettings set "$_gs_schema" "$_gs_key" "$_gs_val" 2>/dev/null; then
                _gs_set=$((_gs_set + 1))
            else
                warn "gsettings set $_gs_schema $_gs_key '$_gs_val' failed"
            fi
        else
            _gs_skip=$((_gs_skip + 1))
        fi
    done < "$_gs_file"
    info "$(basename "$_gs_file"): $_gs_set key(s) applied, $_gs_skip not present in this version"
}

# Behaviour (dotfiles-owned): layout, reading habits, privacy.
osr_gsettings_apply "$OSR_DOTFILES/evolution/gsettings.conf"
# Appearance (rice-owned, swapped on rice switch §6): colors and fonts.
if [ -n "${OSR_THEME_DIR:-}" ]; then
    osr_gsettings_apply "$OSR_THEME_DIR/config/evolution/gsettings.conf"
fi

# --- 2. the scoped GTK theme --------------------------------------------------
#
# A private theme rather than an addition to ~/.config/gtk-3.0/gtk.css: GTK3 has
# no per-application CSS selector, so anything written there would restyle every
# GTK app on the machine. The theme imports Adwaita-dark from GTK's own resource
# bundle and only overrides on top of it.
_ev_theme="$OSR_HOME/.local/share/themes/osr-evolution/gtk-3.0"
if as_user mkdir -p "$_ev_theme" && install_theme_layer evolution gtk.css "$_ev_theme/gtk.css"; then

    # .desktop override that selects it. A user-level copy in
    # ~/.local/share/applications wins over the packaged one without touching
    # /usr, and `update-desktop-database` is what makes the menu notice.
    _ev_apps="$OSR_HOME/.local/share/applications"
    as_user mkdir -p "$_ev_apps"
    _ev_done=""
    for _ev_src in /usr/share/applications/org.gnome.Evolution.desktop \
                   /usr/share/applications/evolution.desktop; do
        [ -f "$_ev_src" ] || continue
        info "installing themed launcher: $(basename "$_ev_src")"
        # Prefix every Exec= with the env that selects the private theme. Leaves
        # the rest of the entry (icon, MIME types, actions) exactly as packaged.
        sed 's|^Exec=|Exec=env GTK_THEME=osr-evolution |' "$_ev_src" \
            | as_user tee "$_ev_apps/$(basename "$_ev_src")" >/dev/null
        _ev_done=1
        break
    done
    # The theme is installed either way, but without the launcher nothing selects
    # it — say so instead of leaving a theme dir nobody reads.
    [ -n "$_ev_done" ] || warn "no Evolution .desktop in /usr/share/applications - theme installed but not selected (rerun this module after installing evolution)"
    command -v update-desktop-database >/dev/null 2>&1 \
        && as_user update-desktop-database "$_ev_apps" >/dev/null 2>&1 || :
fi

# --- 3. mail defaults ---------------------------------------------------------
# Evolution registers itself as a mailto: handler only once it has run. Claim it
# now so a link in the browser opens the client the rice actually installed.
if command -v xdg-mime >/dev/null 2>&1; then
    as_user xdg-mime default org.gnome.Evolution.desktop x-scheme-handler/mailto 2>/dev/null || :
fi
