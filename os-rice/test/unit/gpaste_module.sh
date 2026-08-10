#!/bin/sh
# Proves the GPaste version logic in lib/build.sh: that the tag picked is the
# newest one on the RUNNING gnome-shell's major (not the newest overall — a v50
# Shell must not get v45, and a v45 Shell must not get v50), that the builder
# short-circuits only when the installed client already matches, and that
# modules/gpaste.sh skips its GNOME block off GNOME. Hermetic: no net, no root
# (gnome-shell, gpaste-client, gsettings and the tags API are all mocks).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_DOTFILES OSR_PKG=apt
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"; . "$OSR_LIB/net.sh"
. "$OSR_LIB/build.sh"
. "$HERE/../lib.sh"

BIN=$(mktemp -d)
PATH="$BIN:$PATH"; export PATH

# ---- Mocks ------------------------------------------------------------------
# gnome-shell/gpaste-client report whatever the env var says, or are "absent"
# (exit 1) when it is empty — the same shape as the real absence.
cat >"$BIN/gnome-shell" <<'MOCK_EOF'
#!/bin/sh
[ -n "${MOCK_SHELL_VER:-}" ] || exit 1
echo "GNOME Shell $MOCK_SHELL_VER"
MOCK_EOF
cat >"$BIN/gpaste-client" <<'MOCK_EOF'
#!/bin/sh
[ -n "${MOCK_GPASTE_VER:-}" ] || exit 1
echo "GPaste $MOCK_GPASTE_VER"
MOCK_EOF
chmod +x "$BIN/gnome-shell" "$BIN/gpaste-client"

# osr_fetch_stdout is the only net call in _gpaste_tag; replace it with a canned
# tags payload in upstream's real shape (newest first, majors interleaved).
osr_fetch_stdout() {
    cat <<'JSON'
[ {"name": "v50.7"}, {"name": "v50.6"}, {"name": "v50.10"}, {"name": "v45.11"},
  {"name": "v45.3"}, {"name": "v45"} ]
JSON
}

echo "gpaste: version resolution"

# ---- gnome-shell major ------------------------------------------------------
MOCK_SHELL_VER=50.1 assert_eq 50 "$(MOCK_SHELL_VER=50.1 _gpaste_gnome_major)" \
    "gnome-shell 50.1 -> major 50"
assert_eq 45 "$(MOCK_SHELL_VER=45.9 _gpaste_gnome_major)" \
    "gnome-shell 45.9 -> major 45"
if MOCK_SHELL_VER= _gpaste_gnome_major >/dev/null 2>&1; then
    fail "absent gnome-shell must fail, not report a major"
else
    ok "absent gnome-shell -> non-zero (builder errors out instead of guessing)"
fi

# ---- client major -----------------------------------------------------------
assert_eq 45 "$(MOCK_GPASTE_VER=45.3 _gpaste_client_major)" \
    "gpaste-client 45.3 -> major 45"

# ---- tag selection ----------------------------------------------------------
# The regression this guards: the distro's v45 is "a real GPaste" and v50.7 is
# "the latest GPaste", but only the Shell's own major is the right answer. Note
# v50.10 > v50.7 — a lexical sort would pick v50.7 here, sort -V picks .10.
assert_eq v50.10 "$(_gpaste_tag 50)" "Shell 50 -> newest v50 tag (version sort, not lexical)"
assert_eq v45.11 "$(_gpaste_tag 45)" "Shell 45 -> newest v45 tag, never the newer v50"

# A major upstream has not tagged yet falls back to the newest tag overall
# rather than building nothing.
github_latest() { printf 'v50.7'; }
assert_eq v50.7 "$(_gpaste_tag 51)" "untagged major -> falls back to the latest tag"

# ---- prefix: the typelib has to land where GIRepository looks ---------------
# The regression this guards is silent: a /usr/local prefix produces an install
# that passes every version check and a shell extension that dies on
# "Requiring GPaste, version 2: Typelib file ... not found".
echo "gpaste: install prefix"
BUILD_SRC="$OSR_LIB/build.sh"
assert_contains "$BUILD_SRC" "prefix=/usr[^/]" "meson prefix is /usr, not /usr/local"
refute_contains "$BUILD_SRC" "services-dir=/usr/local" \
    "no /usr/local paths left in the GPaste meson args"

# _gpaste_typelib_ok against a fake root: a typelib present ONLY under
# /usr/local must read as missing, because that is precisely the install that
# looks healthy and cannot load.
FAKE=$(mktemp -d)
mkdir -p "$FAKE/usr/local/lib/$(uname -m)-linux-gnu/girepository-1.0"
touch "$FAKE/usr/local/lib/$(uname -m)-linux-gnu/girepository-1.0/GPaste-2.typelib"
if _gpaste_typelib_ok "$FAKE"; then
    fail "a typelib only under /usr/local must NOT count as findable"
else
    ok "typelib only under /usr/local reads as missing (forces the rebuild)"
fi
mkdir -p "$FAKE/usr/lib/$(uname -m)-linux-gnu/girepository-1.0"
touch "$FAKE/usr/lib/$(uname -m)-linux-gnu/girepository-1.0/GPaste-2.typelib"
if _gpaste_typelib_ok "$FAKE"; then
    ok "typelib in the system libdir reads as findable"
else
    fail "typelib in the system libdir must count as findable"
fi
rm -rf "$FAKE"

# ---- module: non-GNOME skips the whole GNOME block --------------------------
echo "gpaste: module gating"
OUT=$(mktemp)
cat >"$BIN/gsettings" <<'MOCK_EOF'
#!/bin/sh
echo "gsettings $*" >>"$OUT"
MOCK_EOF
cat >"$BIN/gnome-extensions" <<'MOCK_EOF'
#!/bin/sh
echo "gnome-extensions $*" >>"$OUT"
MOCK_EOF
chmod +x "$BIN/gsettings" "$BIN/gnome-extensions"
export OUT

# pkg_install is the installer's job, not this test's — stub it out.
pkg_install() { :; }

( XDG_CURRENT_DESKTOP=Hyprland XDG_SESSION_DESKTOP=hyprland \
  . "$OSR_ROOT/modules/gpaste.sh" ) >/dev/null 2>&1 || true
refute_contains "$OUT" "org.gnome.GPaste" "off GNOME: no GPaste gsettings written"
refute_contains "$OUT" "gnome-extensions enable" "off GNOME: extension not touched"

: >"$OUT"
( XDG_CURRENT_DESKTOP=GNOME XDG_SESSION_DESKTOP=gnome \
  . "$OSR_ROOT/modules/gpaste.sh" ) >/dev/null 2>&1 || true
assert_contains "$OUT" "gnome-extensions enable" "on GNOME: extension enabled"
assert_contains "$OUT" "images-support true" "on GNOME: images-support set (the empty-image-history fix)"

rm -rf "$BIN" "$OUT"
finish
