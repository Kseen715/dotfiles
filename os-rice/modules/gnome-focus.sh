# session: wayland
# modules/gnome-focus.sh — make notification clicks raise the window (DRAFT).
#
# GNOME Shell's focus-stealing prevention: when an app asks for focus without a
# fresh user-interaction timestamp (Telegram, Thunderbird, anything raising a
# window from a tray/notification), the Shell refuses and shows a second
# "<App> is ready" notification instead. Clicking that one finally raises it.
# Two clicks for every message.
#
# There is no gsettings key for this on Wayland — the behaviour lives in
# MetaDisplay's focus policy, so the only fix is a Shell extension that catches
# `demands-attention` and activates the window itself.

_gf_uuid="stealmyfocus@kleinernik.gmail.com"

if ! command -v gnome-shell >/dev/null 2>&1; then
    warn "gnome-shell not found - skipping $_gf_uuid"
    return 0 2>/dev/null || exit 0
fi

# extensions.gnome.org serves a different zip per Shell major, so ask for ours.
_gf_ver=$(gnome-shell --version | sed -n 's/.*[[:space:]]\([0-9][0-9]*\)\..*/\1/p')
_gf_zip="${TMPDIR:-/tmp}/$_gf_uuid.zip"
_gf_api="https://extensions.gnome.org/extension-info/?uuid=$_gf_uuid&shell_version=$_gf_ver"

_gf_install() {
    _gf_url=$(osr_fetch_stdout "$_gf_api" \
        | sed -n 's/.*"download_url"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)
    [ -n "$_gf_url" ] || error "no build of $_gf_uuid for GNOME $_gf_ver"
    osr_download "https://extensions.gnome.org$_gf_url" "$_gf_zip"
    as_user gnome-extensions install --force "$_gf_zip"
    rm -f "$_gf_zip"
}

run_step "Installing Steal My Focus Window" _gf_install

# Enabling only sticks once the Shell has loaded the new extension; on a live
# session that means a logout (Wayland) or Alt+F2 r (X11). Best-effort (§9).
as_user gnome-extensions enable "$_gf_uuid" 2>/dev/null \
    || warn "$_gf_uuid installed but not enabled yet - log out and back in"
