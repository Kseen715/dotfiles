#!/bin/sh
# Proves `osr state` (lib/state.c, in the harness core) reads and writes the
# state file exactly as the pure-sh lib/state.sh did, frozen at
# test/ref/state_sh_ref.sh.
#
# lib/state.sh is GONE: its whole reason to exist was the `as_user tee` write,
# and the core performs that itself now (run_as_user in lib/state.c), so every
# caller talks to `osr state` directly. The last section proves the escalation
# has the same shape the shell function had.
#
# Two things are compared: the BYTES osr_state_get prints, and the BYTES of the
# state file after a sequence of osr_state_set calls. The fixtures cover what
# the sh version's `sed`/`grep`/`$(...)` pipeline actually did: last assignment
# wins, values containing `=` survive, blank and junk lines are preserved,
# trailing blank lines are eaten by the command substitution, a missing file is
# not an error, and a key with a `.` in it is a BASIC REGULAR EXPRESSION (which
# is how `osr_state_get "wallpaper.$OSR_THEME"` in lib/config.sh behaves).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
OSR_REF="$OSR_ROOT/test/ref/state_sh_ref.sh"; export OSR_REF
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

hex() { od -An -tx1 | tr -d ' \n'; }

same() {
    if [ "$2" = "$3" ]; then
        ok "$1"
    else
        fail "$1"
        printf '    ref: %s\n    c  : %s\n' "$2" "$3" >&2
    fi
}

# write_fixture <home> <kind> -- lay down a state file to read back.
write_fixture() {
    mkdir -p "$1/.config/osr"
    case "$2" in
        normal)   printf 'rice=i3-rosemary\ntheme=nord\nwallpaper=/img/a.png\napplied=1754\n' ;;
        dupes)    printf 'theme=nord\ntheme=gruvbox\ntheme=xin\n' ;;
        equals)   printf 'wallpaper=/img/a=b=c.png\ntheme=nord\n' ;;
        blanks)   printf 'rice=i3\n\n\ntheme=nord\n\n\n' ;;
        junk)     printf 'not a key value line\n=leading\nrice=i3\n# comment\n' ;;
        nonl)     printf 'rice=i3\ntheme=nord' ;;
        empty)    : ;;
        dotted)   printf 'wallpaperXnord=/img/decoy.png\nwallpaper.nord=/img/real.png\n' ;;
        missing)  rm -rf "$1/.config/osr" ;;
    esac >"$1/.config/osr/state" 2>/dev/null || :
}

FIXTURES='normal dupes equals blanks junk nonl empty dotted missing'
KEYS='rice theme wallpaper applied absent wallpaper.nord'

# --- 1. osr_state_get --------------------------------------------------------
_get_cases=0
_get_diffs=0
for _fx in $FIXTURES; do
    for _key in $KEYS; do
        _get_cases=$((_get_cases + 1))
        rm -rf "$TMP/ref" "$TMP/c"
        mkdir -p "$TMP/ref" "$TMP/c"
        write_fixture "$TMP/ref" "$_fx"
        write_fixture "$TMP/c" "$_fx"
        _r=$(OSR_HOME="$TMP/ref" sh -c '. "$OSR_REF"; osr_state_get "$1"' _ "$_key" | hex)
        _c=$(OSR_HOME="$TMP/c" "$OSR_BIN" state get "$_key" | hex)
        if [ "$_r" != "$_c" ]; then
            _get_diffs=$((_get_diffs + 1))
            printf '    get diff: fixture=%s key=%s\n      ref: %s\n      c  : %s\n' \
                "$_fx" "$_key" "$_r" "$_c" >&2
        fi
    done
done
assert_eq 0 "$_get_diffs" "osr_state_get: $_get_cases fixture/key pairs byte-identical"

# --- 2. osr_state_set --------------------------------------------------------
# as_user is lib/user.sh's, and the whole point of the shim keeping the write:
# stub it with a plain exec so the test needs no sudo, exactly as the modules'
# unit tests do.
AS_USER='as_user() { "$@"; }'
_set_cases=0
_set_diffs=0
for _fx in $FIXTURES; do
    for _pair in 'theme gruvbox' 'rice arch-hyprland-glass' 'newkey newvalue' \
                 'wallpaper /img/a=b.png' 'applied 1755000000' 'wallpaper.nord /img/x.png'; do
        _set_cases=$((_set_cases + 1))
        # shellcheck disable=SC2086  # deliberate split into "key value"
        set -- $_pair
        rm -rf "$TMP/ref" "$TMP/c"
        mkdir -p "$TMP/ref" "$TMP/c"
        write_fixture "$TMP/ref" "$_fx"
        write_fixture "$TMP/c" "$_fx"
        OSR_HOME="$TMP/ref" sh -c '. "$OSR_REF"; '"$AS_USER"'; osr_state_set "$1" "$2"' _ "$1" "$2"
        OSR_HOME="$TMP/c" "$OSR_BIN" state set "$1" "$2"
        _r=$(hex <"$TMP/ref/.config/osr/state")
        _c=$(hex <"$TMP/c/.config/osr/state")
        if [ "$_r" != "$_c" ]; then
            _set_diffs=$((_set_diffs + 1))
            printf '    set diff: fixture=%s key=%s value=%s\n      ref: %s\n      c  : %s\n' \
                "$_fx" "$1" "$2" "$_r" "$_c" >&2
        fi
    done
done
assert_eq 0 "$_set_diffs" "osr_state_set: $_set_cases fixture/write pairs produce identical files"

# --- 3. a real sequence, the one install.sh performs -------------------------
SEQ='osr_state_set rice i3-rosemary; osr_state_set theme nord; osr_state_set applied 1754000000;
     osr_state_set theme gruvbox; osr_state_set wallpaper /img/b.png;
     printf "get:%s|%s|%s|%s\n" "$(osr_state_get rice)" "$(osr_state_get theme)" \
        "$(osr_state_get wallpaper)" "$(osr_state_get applied)"'
SEQ_C='B=$OSR_BIN; $B state set rice i3-rosemary; $B state set theme nord;
     $B state set applied 1754000000; $B state set theme gruvbox;
     $B state set wallpaper /img/b.png;
     printf "get:%s|%s|%s|%s\n" "$($B state get rice)" "$($B state get theme)" \
        "$($B state get wallpaper)" "$($B state get applied)"'
rm -rf "$TMP/ref" "$TMP/c"; mkdir -p "$TMP/ref" "$TMP/c"
_r=$(OSR_HOME="$TMP/ref" sh -c '. "$OSR_REF"; '"$AS_USER"'; '"$SEQ" | hex)
_c=$(OSR_HOME="$TMP/c" sh -c "$SEQ_C" | hex)
same "install sequence: reads back identically" "$_r" "$_c"
same "install sequence: file identical" \
    "$(hex <"$TMP/ref/.config/osr/state")" "$(hex <"$TMP/c/.config/osr/state")"

# --- 4. osr_state_file -------------------------------------------------------
for _home in "$TMP/some home" '' ; do
    _r=$(OSR_HOME="$_home" sh -c '. "$OSR_REF"; osr_state_file' | hex)
    _c=$(OSR_HOME="$_home" "$OSR_BIN" state file | hex)
    same "osr_state_file: OSR_HOME='$_home'" "$_r" "$_c"
done

# --- 5. the directory is created as the user ---------------------------------
# The write goes through `as_user mkdir -p` + `as_user tee`; a fresh HOME must
# end up with the same tree in both.
rm -rf "$TMP/ref" "$TMP/c"; mkdir -p "$TMP/ref" "$TMP/c"
OSR_HOME="$TMP/ref" sh -c '. "$OSR_REF"; '"$AS_USER"'; osr_state_set theme nord'
OSR_HOME="$TMP/c" "$OSR_BIN" state set theme nord
same "fresh HOME: same tree" \
    "$(cd "$TMP/ref" && find . | sort | hex)" "$(cd "$TMP/c" && find . | sort | hex)"

# --- 6. the escalation the shell function used to perform --------------------
# When the installer is not already $OSR_USER, the sh version ran
# `as_user mkdir -p <dir>` and piped the content through `as_user tee <file>`,
# i.e. `sudo -u <user> ...`. The core does it itself now; a stub sudo records
# what it was asked to do, and the file it writes must still be the same.
mkdir -p "$TMP/sudobin" "$TMP/esc"
cat >"$TMP/sudobin/sudo" <<'EOF'
#!/bin/sh
printf 'SUDO %s\n' "$*" >>"$SUDO_LOG"
[ "$1" = -u ] || exit 1
shift 2
exec "$@"
EOF
chmod +x "$TMP/sudobin/sudo"
SUDO_LOG="$TMP/sudo.log"; : >"$SUDO_LOG"; export SUDO_LOG
PATH="$TMP/sudobin:$PATH" OSR_HOME="$TMP/esc" OSR_USER="not-$(id -un)" \
    "$OSR_BIN" state set theme nord
assert_eq "SUDO -u not-$(id -un) mkdir -p $TMP/esc/.config/osr" "$(sed -n 1p "$SUDO_LOG")" \
    "escalates the mkdir through sudo -u"
assert_eq "SUDO -u not-$(id -un) tee $TMP/esc/.config/osr/state" "$(sed -n 2p "$SUDO_LOG")" \
    "escalates the write through sudo -u tee"
assert_eq "theme=nord" "$(cat "$TMP/esc/.config/osr/state" 2>/dev/null)" \
    "and the file it wrote is the composed one"

finish
