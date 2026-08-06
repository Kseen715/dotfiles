#!/bin/sh
# Covers the i3/X11 desktop slice:
#   1. servicemap @init facets (bluetooth/cups differ on runit only)
#   2. xbps.map rows for the Void names that actually differ
#   3. modules/i3.sh config layering (§5): base, rice theme, machine-local
#   4. the void-i3-rosemuted rice manifest only names modules that exist
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/pkg.sh"; . "$OSR_LIB/service.sh"
. "$HERE/../lib.sh"

# --- 1. servicemap @init facet ------------------------------------------------
# The whole point: one logical name, a per-init unit name, no case in a module.
OSR_INIT=runit;   export OSR_INIT
assert_eq bluetoothd "$(service_resolve bluetooth)" "runit: bluetooth -> bluetoothd"
assert_eq cupsd      "$(service_resolve cups)"      "runit: cups -> cupsd"
assert_eq smbd       "$(service_resolve smb)"       "runit: smb -> smbd"
assert_eq NetworkManager "$(service_resolve NetworkManager)" "runit: unmapped name passes through"

OSR_INIT=systemd; export OSR_INIT
assert_eq bluetooth "$(service_resolve bluetooth)" "systemd: bluetooth is untouched by the runit row"
assert_eq cups      "$(service_resolve cups)"      "systemd: cups is untouched by the runit row"

# --- 2. xbps.map rows ---------------------------------------------------------
OSR_PKG=xbps; export OSR_PKG
OSR_CODENAME=''; OSR_VERSION_ID=''; OSR_ARCH=x86_64
export OSR_CODENAME OSR_VERSION_ID OSR_ARCH

check_map() { assert_eq "$2" "$(_pkgmap_one "$1")" "xbps: $1 -> $2"; }
check_map thunar Thunar                                  # upstream capitalisation
check_map copyq CopyQ
check_map obs-studio obs
check_map sshfs fuse-sshfs
check_map xorg-xrandr xrandr                             # Void drops the xorg- prefix
check_map xorg-xinit xinit
check_map gst-plugins-good gst-plugins-good1             # GStreamer carries the API version
check_map vulkan-icd-loader vulkan-loader
check_map gvfs-nfs nfs-utils                             # no gvfs NFS backend on Void
check_map raw-thumbnailer libopenraw-pixbuf-loader       # not packaged; pixbuf loader instead
check_map networkmanager NetworkManager                  # modules/networkmanager.sh compat
check_map xidlehook cargo:xidlehook                      # not packaged -> provider row
# Names that are identical on Void must NOT have a row (§1: no identity rows).
check_map picom picom
check_map polybar polybar
check_map i3 i3

# --- 3. modules/i3.sh layering ------------------------------------------------
T=$(mktemp -d)
OSR_HOME="$T/home"; OSR_USER=$(id -un)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_RICE_DIR="$OSR_ROOT/rices/void-i3-rosemuted"
export OSR_HOME OSR_USER OSR_DOTFILES OSR_RICE_DIR
mkdir -p "$OSR_HOME"
. "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"
as_user()  { "$@"; }
as_root()  { echo "as_root $*" >> "$T/root-calls"; }
CALLS="$T/pkg-calls"; : > "$CALLS"
pkg_install() { echo "$*" >> "$CALLS"; }
run_step()    { shift; "$@"; }

. "$OSR_ROOT/modules/i3.sh"

assert_contains "$CALLS" "i3 i3status dex" "i3 module installs i3 + the companions its config execs"
[ -f "$OSR_HOME/.config/i3/config" ] && ok "base config installed (dotfiles layer)" \
    || fail "base config installed (dotfiles layer)"
assert_contains "$OSR_HOME/.config/i3/config" 'include ~/.config/i3/config.d/\*.conf' \
    "base config ends with the layer include"
assert_contains "$OSR_HOME/.config/i3/config.d/90-theme.conf" "client.focused" \
    "rice theme layer installed (90)"
[ -f "$OSR_HOME/.config/i3/config.d/99-local.conf" ] && ok "machine layer seeded empty (99)" \
    || fail "machine layer seeded empty (99)"
# The wallpaper placeholder must be substituted, never installed literally.
refute_contains "$OSR_HOME/.config/i3/config.d/90-theme.conf" "{{WALLPAPER_PATH}}" \
    "wallpaper placeholder substituted in the theme layer"
# The theme layer must not carry keybinds: a rice switch may not change your keys.
refute_contains "$OSR_HOME/.config/i3/config.d/90-theme.conf" "^bindsym" \
    "theme layer carries no keybinds (§5 ownership split)"

# --- 3b. compose_json_config: rice theme keys merged over the dotfiles base ---
# The editors that keep everything in one settings.json have no include, so the
# §5 split is done by composition. The base must survive, the rice must win.
printf '{"a":1,"colorscheme":"base","keep":"yes"}\n' > "$T/base.json"
printf '{"colorscheme":"rosemuted"}\n' > "$T/frag.json"
compose_json_config "$T/base.json" "$T/frag.json" "$T/out.json"
assert_eq rosemuted "$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["colorscheme"])' "$T/out.json")" \
    "rice fragment overrides the base key"
assert_eq yes "$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["keep"])' "$T/out.json")" \
    "base keys the rice does not mention survive"
# No fragment (a rice that ships no theme for this app) must still install.
compose_json_config "$T/base.json" "" "$T/out2.json"
assert_eq base "$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["colorscheme"])' "$T/out2.json")" \
    "missing rice fragment installs the base unthemed"

# --- 3c. install_mozilla_layer: every profile, resolved from profiles.ini -----
# A Mozilla profile dir has a random name, so this is the only way the prefs and
# the theme reach the browser at all.
MOZ="$T/moz"
mkdir -p "$MOZ/abc123.default-release" "$MOZ/xyz789.dev-edition"
cat > "$MOZ/profiles.ini" <<'INI'
[Profile0]
Name=default-release
IsRelative=1
Path=abc123.default-release

[Profile1]
Name=dev
IsRelative=1
Path=xyz789.dev-edition
INI
printf 'user_pref("dom.ipc.processCount", 2);\n' > "$T/user.js"
printf '#nav-bar { background: #1c1f20; }\n'     > "$T/userChrome.css"
install_mozilla_layer "$MOZ" "$T/user.js" "$T/userChrome.css" >/dev/null 2>&1
assert_contains "$MOZ/abc123.default-release/user.js" "dom.ipc.processCount" \
    "user.js installed into the release profile"
assert_contains "$MOZ/xyz789.dev-edition/user.js" "dom.ipc.processCount" \
    "user.js installed into the second profile too"
assert_contains "$MOZ/abc123.default-release/chrome/userChrome.css" "nav-bar" \
    "userChrome.css installed under chrome/"
# The prefs that make userChrome.css load at all must ship with it.
assert_contains "$OSR_ROOT/../firefox/user.js" "legacyUserProfileCustomizations" \
    "firefox user.js enables userChrome.css (or the rice theme is dead weight)"
# A machine that never launched Firefox must warn, not fail.
install_mozilla_layer "$T/never-launched" "$T/user.js" "" >/dev/null 2>&1
assert_eq 0 "$?" "a profile-less app warns instead of failing the module"

# --- 3d. evolution: the gsettings applier ------------------------------------
# Two things have to hold: a key this Evolution version does not have is skipped
# rather than failing the module, and a '#d98cae' VALUE is not mistaken for a
# comment (the first cut of the parser ate every hex color).
GSBIN="$T/gsbin"; mkdir -p "$GSBIN"
cat > "$GSBIN/gsettings" <<'GSEOF'
#!/bin/sh
case "$1" in
  list-keys) case "$2" in
      org.gnome.evolution.mail) printf 'layout\ncitation-color\n' ;;
      *) exit 1 ;; esac ;;
  set) echo "SET $2 $3 $4" >> "$GS_LOG" ;;
esac
GSEOF
chmod +x "$GSBIN/gsettings"
GS_LOG="$T/gs.log"; : > "$GS_LOG"; export GS_LOG
PATH="$GSBIN:$PATH"; export PATH

cat > "$T/gs.conf" <<'GSCONF'
# a whole-line comment
org.gnome.evolution.mail layout 1
org.gnome.evolution.mail citation-color '#d98cae'   # a trailing comment
org.gnome.evolution.mail no-such-key-in-this-version true
org.gnome.evolution.shell menubar-visible false
GSCONF

# The applier is defined by the module, so source it with the side effects mocked.
( pkg_install() { :; }
  run_step() { shift; "$@"; }
  OSR_RICE_DIR=""
  . "$OSR_ROOT/modules/evolution.sh"
  osr_gsettings_apply "$T/gs.conf" ) >/dev/null 2>&1

assert_contains "$GS_LOG" "citation-color '#d98cae'" \
    "a hex color value survives comment stripping"
refute_contains "$GS_LOG" "trailing comment" "a trailing # comment is stripped"
refute_contains "$GS_LOG" "no-such-key" "a key this version lacks is skipped, not set"
refute_contains "$GS_LOG" "menubar-visible" "a schema this version lacks is skipped"
assert_contains "$GS_LOG" "layout 1" "known keys are still applied"

# The rice must ship the scoped Evolution theme, not a global gtk.css edit —
# GTK3 has no per-app selector, so a global one would restyle every GTK app.
assert_contains "$OSR_ROOT/rices/void-i3-rosemuted/config/evolution/gtk.css" \
    "resource:///org/gtk/libgtk/theme/Adwaita" "the Evolution theme extends Adwaita rather than replacing it"
assert_contains "$OSR_ROOT/modules/evolution.sh" "GTK_THEME=osr-evolution" \
    "the .desktop override is what scopes the theme to Evolution"

# --- 3e. VS Code is installed but never config-managed ------------------------
# It has its own Settings Sync; two managers on one settings.json means the last
# writer wins and the other silently loses edits.
refute_contains "$OSR_ROOT/modules/vscode.sh" "install_layer" \
    "vscode module writes no config layer"
refute_contains "$OSR_ROOT/modules/vscode.sh" "compose_json_config" \
    "vscode module composes no settings.json"
[ -d "$OSR_ROOT/rices/void-i3-rosemuted/config/vscode" ] \
    && fail "the rice still ships a VS Code palette" \
    || ok "the rice ships no VS Code palette"

# --- 4. the rice manifest names only modules that exist -----------------------
_missing=""
while IFS= read -r _l || [ -n "$_l" ]; do
    _l=${_l%%#*}
    _l=$(printf '%s' "$_l" | tr -d ' \t')
    [ -n "$_l" ] || continue
    case "$_l" in require:*|config:*) continue ;; esac
    [ -f "$OSR_ROOT/modules/$_l.sh" ] || _missing="$_missing $_l"
done < "$OSR_RICE_DIR/rice.list"
assert_eq "" "$_missing" "every module in void-i3-rosemuted/rice.list exists"

rm -rf "$T"
finish
