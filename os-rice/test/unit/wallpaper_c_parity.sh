#!/bin/sh
# Proves the wallpaper front end behaves byte-for-byte like the pure-sh one it
# replaced, frozen at test/ref/wallpaper_sh_ref.sh.
#
# wallpaper.sh is a shim now: the option loop, the current-theme resolution and
# the four actions (show, --list, --next, set) are `osr wallpaper` in the
# harness core (lib/wallpaper_front.c). The wallpaper family underneath -
# resolve, install, record, set-live, library, choose - had already moved to
# lib/config.c, and test/unit/config_c_parity.sh is what holds THAT half
# together; this file is about the front end around it, end to end.
#
# Both runners execute inside a SANDBOX ROOT that symlinks the real lib/ and
# build/ and carries its own themes/, with:
#
#   - PATH reduced to a stub bin, so no wallpaper setter exists (swww, hyprctl
#     and feh are all absent: the headless branch is the one a test can take);
#   - OSR_PASSWD_FILE pointing at a fake passwd whose home is inside the
#     sandbox, which is what keeps a picked wallpaper out of the real
#     ~/Pictures/Wallpapers - both sides resolve $OSR_HOME from passwd;
#   - a fixture theme whose wallpapers/ holds two images and one README.txt,
#     so the extension filter has something to reject.
#
# Compared: the help text, the query with and without a recorded pick, the
# library listing, the wrap-around --next over a full cycle, setting a path
# from outside the theme, and every error path - stdout, stderr, exit status
# and the resulting $HOME tree each time.
#
# TWO divergences are asserted rather than hidden:
#
#   1. `--user` with no operand used to hit `${2:?--user needs a name}`, whose
#      message and exit status came from the shell itself; it is now a normal
#      `error` line, exactly as install.sh's port made it.
#   2. --next walked `for _img in $(osr_wallpaper_library)`, so a path with a
#      space in it was two entries and neither existed. The C walks the library
#      a line at a time. Asserted at the end of this file.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip wallpaper_c_parity: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
ROOT="$TMP/root"
BIN="$TMP/bin"; mkdir -p "$BIN"

# hex — compare bytes, not lines: a trailing space or a lost newline is exactly
# the kind of difference this file exists to catch.
hex() { od -An -tx1 -v | tr -d ' \n'; }
same() { assert_eq "$2" "$3" "$1"; }

# --- the stub bin ------------------------------------------------------------
for _t in sh env cat grep sed awk printf id rm mkdir mktemp test true false tee \
          cp chmod touch cut tr head tail sort wc dirname basename find od ln; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$BIN/$_t" ;; esac
done
# sudo runs the command rather than escalating: the sandbox's $OSR_USER is not
# a real account, and every write here lands inside the sandbox anyway.
cat >"$BIN/sudo" <<'SUDOEOF'
#!/bin/sh
[ "$1" = "-u" ] && shift 2
exec "$@"
SUDOEOF
chmod +x "$BIN/sudo"
# swww / hyprctl / feh are absent on purpose: with no setter, both sides take
# the headless branch, which records the pick and says so. A real setter would
# repaint the developer's desktop from a unit test.

# --- the sandbox root --------------------------------------------------------
mkdir -p "$ROOT/lib" "$ROOT/themes/nord/wallpapers" "$ROOT/themes/bare/wallpapers" \
         "$TMP/extra"
for _f in "$OSR_LIB"/*.sh; do ln -sfn "$_f" "$ROOT/lib/$(basename "$_f")"; done
ln -sfn "$OSR_ROOT/build" "$ROOT/build"
cp "$OSR_ROOT/wallpaper.sh" "$ROOT/wallpaper.sh"
cp "$OSR_ROOT/test/ref/wallpaper_sh_ref.sh" "$ROOT/wallpaper_ref.sh"

# Two themes: one with images, one whose wallpapers/ holds only a placeholder.
printf 'display: nord\n' >"$ROOT/themes/nord/theme.list"
printf 'display: bare\n' >"$ROOT/themes/bare/theme.list"
img() { printf 'PNG-BYTES-%s\n' "$2" >"$1"; }
img "$ROOT/themes/nord/wallpapers/01-first.png" one
img "$ROOT/themes/nord/wallpapers/02-second.jpg" two
printf 'drop a real image here\n' >"$ROOT/themes/nord/wallpapers/README.txt"
printf 'drop a real image here\n' >"$ROOT/themes/bare/wallpapers/README.txt"
img "$TMP/extra/outside.png" outside

# Two homes, one per side, so neither run can see what the other wrote.
for _s in ref c; do
    mkdir -p "$TMP/$_s/home"
    printf 'tester:x:1000:1000::%s:/bin/sh\n' "$TMP/$_s/home" >"$TMP/$_s/passwd"
done

# reset_homes [theme] — both sides start from the same empty home, with the
# named theme recorded (or none at all).
reset_homes() {
    for _s in ref c; do
        rm -rf "${TMP:?}/$_s/home"; mkdir -p "$TMP/$_s/home/.config/osr"
        [ -z "${1:-}" ] || printf 'theme=%s\n' "$1" >"$TMP/$_s/home/.config/osr/state"
    done
}

# run_both <label> <args...> — run the frozen sh original and the shim with
# identical environments, compare stdout, stderr and exit status.
run_both() {
    _label=$1
    _ref_rc=0; _c_rc=0
    shift
    for _s in ref c; do
        _script=$ROOT/wallpaper_ref.sh
        [ "$_s" = ref ] || _script=$ROOT/wallpaper.sh
        _rc=0
        env -i PATH="$BIN" USER=tester HOME="$TMP/$_s/home" \
            OSR_PASSWD_FILE="$TMP/$_s/passwd" \
            OSR_LOG="$TMP/$_s/run.log" NO_COLOR=1 TERM=dumb \
            sh "$_script" "$@" >"$TMP/$_s.out" 2>"$TMP/$_s.err" </dev/null || _rc=$?
        eval "_${_s}_rc=\$_rc"
    done
    # The two runs write into different homes; that path is the only thing
    # allowed to differ, so it is collapsed before the bytes are compared.
    for _f in out err; do
        sed "s#$TMP/ref#SBOX#g" "$TMP/ref.$_f" >"$TMP/ref.$_f.n"
        sed "s#$TMP/c#SBOX#g"   "$TMP/c.$_f"   >"$TMP/c.$_f.n"
        same "$_label: std$_f" "$(hex <"$TMP/ref.$_f.n")" "$(hex <"$TMP/c.$_f.n")"
    done
    assert_eq "$_ref_rc" "$_c_rc" "$_label: exit status ($_ref_rc)"
}

# tree_same <label> — the two homes hold the same files, with the same bytes.
tree_same() {
    assert_eq "$( (cd "$TMP/ref/home" && find . -type f | sort) )" \
              "$( (cd "$TMP/c/home" && find . -type f | sort) )" \
              "$1: the same files land in \$HOME"
    _diff=""
    for _f in $( (cd "$TMP/ref/home" && find . -type f | sort) ); do
        cmp -s "$TMP/ref/home/$_f" "$TMP/c/home/$_f" 2>/dev/null \
            || sed "s#$TMP/ref#SBOX#g" "$TMP/ref/home/$_f" >"$TMP/ref.f" 2>/dev/null || :
        if [ -f "$TMP/c/home/$_f" ]; then
            sed "s#$TMP/ref#SBOX#g" "$TMP/ref/home/$_f" >"$TMP/ref.f"
            sed "s#$TMP/c#SBOX#g"   "$TMP/c/home/$_f"   >"$TMP/c.f"
            cmp -s "$TMP/ref.f" "$TMP/c.f" || _diff="$_diff [$_f]"
        fi
    done
    assert_eq "" "$_diff" "$1: with the same bytes"
}

# --- 1. the help text --------------------------------------------------------
reset_homes nord
run_both "--help" --help
run_both "-h" -h
# The help IS the file's header comment on the sh side (`sed -n '2,9p'`), so a
# comment edit there and a string edit here can drift apart. This is the check
# that catches it.
assert_eq "$(sed -n '2,9p' "$ROOT/wallpaper_ref.sh" | sed 's/^# \{0,1\}//')" \
          "$(cat "$TMP/c.out")" "--help: the C text is the sh header comment"

# --- 2. the query ------------------------------------------------------------
# With nothing picked, the theme's first image is the answer.
reset_homes nord
run_both "show: the theme default"
assert_contains "$TMP/c.out" "01-first.png" "show: the theme's first image"
# A theme with no images at all answers "(none)", not an error.
reset_homes bare
run_both "show: a theme with no images"
assert_eq "(none)" "$(cat "$TMP/c.out")" "show: nothing to show says so"
# With no theme recorded, the default theme decides. $OSR_DEFAULT_THEME is not
# set in the sandbox, so both sides fall back to their built-in default - and a
# default that does not exist in this sandbox's themes/ is the error path below.
reset_homes
run_both "show: no theme recorded"

# --- 3. the listing ----------------------------------------------------------
reset_homes nord
run_both "--list" --list
assert_contains "$TMP/c.out" "01-first.png" "--list: the theme's images are in it"
refute_contains "$TMP/c.out" "README.txt" "--list: a .txt is not a wallpaper"

# --- 4. --next, over a whole cycle -------------------------------------------
# Each step is compared on its own, and the state it leaves is the next step's
# input: a wrap-around that only matched at the end would pass a single-step
# test and fail here.
reset_homes nord
run_both "--next (1 of 3)" --next
tree_same "--next (1 of 3)"
run_both "--next (2 of 3)" --next
tree_same "--next (2 of 3)"
run_both "--next (3 of 3): wraps" --next
tree_same "--next (3 of 3): wraps"
# The library the cycle walks is two images, so three steps must have visited
# both and come back.
assert_contains "$TMP/c.out" "png\|jpg" "--next: yields an image"
# A theme with nothing to step through is an error, not a silent no-op.
reset_homes bare
run_both "--next: no wallpapers" --next

# --- 5. setting one ----------------------------------------------------------
reset_homes nord
run_both "set an image from outside the theme" "$TMP/extra/outside.png"
tree_same "set an image from outside the theme"
# ...and it is what the query answers afterwards.
run_both "show: after a set"
assert_contains "$TMP/c.out" "outside.png" "set: the pick is what show reports"
# The pick is keyed by theme: recording another theme and asking again must not
# hand back the image that was picked for nord.
for _s in ref c; do printf 'theme=bare\n' >>"$TMP/$_s/home/.config/osr/state"; done
run_both "show: the pick is per-theme"
assert_eq "(none)" "$(cat "$TMP/c.out")" "set: another theme keeps its own answer"
# An empty operand is a path, not a question - the sh original took the `*)` arm
# for it, and so does this.
run_both "an empty operand is a (missing) path" ''

# --- 6. every error path -----------------------------------------------------
reset_homes nord
run_both "unknown option" --nope
run_both "unknown short option" -x
run_both "no such file" "$TMP/extra/nothing-here.png"
run_both "not an image" "$ROOT/themes/nord/wallpapers/README.txt"
# A recorded theme that has been deleted since: the library it would offer would
# be another theme's, so this stops rather than guessing.
reset_homes nosuchtheme
run_both "recorded theme is gone" ''

# --- 7. the divergences, asserted --------------------------------------------
# An option missing its operand: the frozen runner died inside the shell's own
# ${x:?...} expansion; the core prints a normal error line.
reset_homes nord
_rc=0
env -i PATH="$BIN" USER=tester HOME="$TMP/c/home" OSR_PASSWD_FILE="$TMP/c/passwd" \
    NO_COLOR=1 TERM=dumb sh "$ROOT/wallpaper.sh" --user >"$TMP/c.out" 2>"$TMP/c.err" \
    </dev/null || _rc=$?
assert_contains "$TMP/c.err" "user needs a name" "missing operand: says which option"
assert_eq 1 "$_rc" "missing operand: exits 1 (the frozen runner exited 2, from the shell)"

# A wallpaper whose path contains a space. The frozen runner's --next split the
# library on whitespace, so this image was two non-existent entries; the C walks
# it a line at a time and steps onto it.
img "$ROOT/themes/nord/wallpapers/03 spaced.png" spaced
reset_homes nord
_rc=0
env -i PATH="$BIN" USER=tester HOME="$TMP/c/home" OSR_PASSWD_FILE="$TMP/c/passwd" \
    NO_COLOR=1 TERM=dumb sh "$ROOT/wallpaper.sh" --list >"$TMP/c.out" 2>"$TMP/c.err" \
    </dev/null || _rc=$?
assert_eq 0 "$_rc" "a path with a space: --list still exits 0"
assert_contains "$TMP/c.out" "03 spaced.png" "a path with a space: the library keeps it whole"
_steps=0
_seen=""
while [ "$_steps" -lt 3 ]; do
    _out=$(env -i PATH="$BIN" USER=tester HOME="$TMP/c/home" \
        OSR_PASSWD_FILE="$TMP/c/passwd" NO_COLOR=1 TERM=dumb \
        sh "$ROOT/wallpaper.sh" --next 2>/dev/null </dev/null)
    _seen="$_seen
$_out"
    _steps=$((_steps + 1))
done
case "$_seen" in
    *"03 spaced.png"*) ok "a path with a space: --next steps onto it (sh split it in two)" ;;
    *) fail "a path with a space: --next never reached it" ;;
esac
rm -f "$ROOT/themes/nord/wallpapers/03 spaced.png"

finish
