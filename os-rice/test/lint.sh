#!/bin/sh
# test/lint.sh — POSIX-sh lint for the harness (§9). Every lib/, module,
# runner and bootstrap file must parse under `dash -n` (busybox ash compat) and,
# when available, pass `shellcheck -s sh`. zsh rc.d layers are checked with
# `zsh -n` when zsh is present.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/.." && pwd)
REPO=$(cd -- "$OSR_ROOT/.." && pwd)

SH_CHECKER=$(command -v dash || command -v sh)
FAILED=0

# Same palette as the installer, same §3 auto-degrade (TTY + NO_COLOR).
. "$OSR_ROOT/lib/ui.sh"
sec()    { printf '%b%s%b\n' "$OSR_CYAN" "$*" "$OSR_NC"; }
p_ok()   { printf '  %bok%b   %s\n' "$OSR_GREEN" "$OSR_NC" "$*"; }
p_warn() { printf '  %bWARN%b %s\n' "$OSR_YELLOW" "$OSR_NC" "$*"; }
p_fail() { printf '  %bFAIL%b %s\n' "$OSR_RED" "$OSR_NC" "$*" >&2; }

sec "POSIX sh syntax ($SH_CHECKER -n):"
SH_FILES="$OSR_ROOT/install.sh $OSR_ROOT/osr $OSR_ROOT/bootstrap.sh"
SH_FILES="$SH_FILES $(find "$OSR_ROOT/lib" "$OSR_ROOT/modules" "$OSR_ROOT/test" -name '*.sh' 2>/dev/null)"
for f in $SH_FILES; do
    if "$SH_CHECKER" -n "$f" 2>/dev/null; then
        p_ok "${f#"$REPO"/}"
    else
        p_fail "${f#"$REPO"/}"
        "$SH_CHECKER" -n "$f" || true
        FAILED=1
    fi
done

if command -v shellcheck >/dev/null 2>&1; then
    # Excluded codes are structural false positives for this codebase, not
    # findings to fix: SC1090/SC1091 dynamic sources; SC1003 the ASCII spinner
    # frames '|/-\'; SC1087 `$var[` inside a bracket-expression regex (sh has
    # no arrays); SC2016 the single-quoted in-container script in matrix.sh;
    # SC2034 vars a sourced lib reads (NO_COLOR, OSR_*); SC2044 find loops over
    # repo-controlled paths; SC2015 the `grep -q X && ok || fail` test idiom;
    # SC2329 module stubs called indirectly by the code under test; SC2163
    # lib/detect.sh's `export $*` over a caller-supplied list of names.
    SC_EXCLUDE=SC1090,SC1091,SC1003,SC1087,SC2016,SC2034,SC2044,SC2015,SC2329,SC2163
    sec "shellcheck -s sh:"
    for f in $SH_FILES; do
        if shellcheck -s sh -e "$SC_EXCLUDE" "$f" >/dev/null 2>&1; then
            p_ok "${f#"$REPO"/}"
        else
            p_warn "${f#"$REPO"/} (shellcheck findings)"
        fi
    done
else
    p_warn "shellcheck: not installed - skipping"
fi

# ASCII-only program output (§3): every byte the installer writes to the
# terminal must be 7-bit ASCII so barebone TERM/locales never mangle it into
# mojibake. Comments are exempt (prose may use §/em-dashes), so skip comment
# lines; flag any high byte (0x80-0xFF) on a code line. LC_ALL=C keeps the byte
# class portable across gawk/mawk/busybox awk.
sec "ASCII-only program output (non-comment lines):"
# Scope: the installer program (lib + modules + runners), not the test harness
# (matrix.sh legitimately keeps em-dashes in trailing comments).
ASCII_FILES="$OSR_ROOT/install.sh $OSR_ROOT/osr $OSR_ROOT/bootstrap.sh"
ASCII_FILES="$ASCII_FILES $(find "$OSR_ROOT/lib" "$OSR_ROOT/modules" -name '*.sh' 2>/dev/null)"
# shellcheck disable=SC2086  # intentional word-split into a file list
_ascii_hits=$(LC_ALL=C awk '
    /^[[:space:]]*#/ { next }
    /[\200-\377]/    { printf "  FAIL %s:%d: %s\n", FILENAME, FNR, $0 }
' $ASCII_FILES 2>/dev/null)
if [ -n "$_ascii_hits" ]; then
    printf '%s\n' "$_ascii_hits" >&2
    FAILED=1
else
    p_ok "(no non-ASCII bytes in program output)"
fi

# Every module declares which display server it supports on its first line, so
# `grep -l '^# session: wayland' modules/*.sh` answers "what breaks if I move
# this rice to X11" without reading 97 files. Enforced, because a marker that is
# only usually there is not something a rice author can rely on.
sec "module session markers (# session: x11 | wayland | x11+wayland):"
_marker_bad=""
for f in "$OSR_ROOT"/modules/*.sh; do
    [ -f "$f" ] || continue
    case "$(head -n 1 "$f")" in
        "# session: x11"|"# session: wayland"|"# session: x11+wayland") ;;
        *) _marker_bad="$_marker_bad ${f#"$REPO"/}" ;;
    esac
done
if [ -n "$_marker_bad" ]; then
    for f in $_marker_bad; do p_fail "$f: missing or invalid '# session:' first line"; done
    FAILED=1
else
    p_ok "(all $(ls "$OSR_ROOT"/modules/*.sh | wc -l | tr -d ' ') modules declare a session)"
fi

# Every shell module is legacy: the C tier (modules/<name>.c + lib/modules.c) is
# where a module belongs, and the marker is what makes the remaining work
# countable - `grep -c '^# legacy:' modules/*.sh` only ever goes down. Enforced
# for the same reason `# session:` is: a marker that is only usually there
# answers no question. See DESIGN §11a.
sec "module legacy markers (# legacy: sh, every .sh module):"
_legacy_bad=""
for f in "$OSR_ROOT"/modules/*.sh; do
    [ -f "$f" ] || continue
    # Header block only (the leading run of comment lines), so a `# legacy:`
    # mentioned in prose further down is not mistaken for the marker.
    sed -n '/^#/!q;p' "$f" | grep -q '^# legacy: sh' \
        || _legacy_bad="$_legacy_bad ${f#"$REPO"/}"
done
if [ -n "$_legacy_bad" ]; then
    for f in $_legacy_bad; do p_fail "$f: missing '# legacy: sh' marker (DESIGN §11a)"; done
    FAILED=1
else
    p_ok "(all $(ls "$OSR_ROOT"/modules/*.sh | wc -l | tr -d ' ') shell modules marked legacy)"
fi

# The converse: a module that HAS been ported must not leave the .sh behind, or
# `osr module has` silently prefers the C one and the stale script rots unread.
# The frozen reference lives at test/ref/<name>_sh_ref.sh instead.
sec "no .sh shadowing a C module:"
_shadow=""
for _m in $("$OSR_BIN" module list 2>/dev/null); do
    [ -f "$OSR_ROOT/modules/$_m.sh" ] && _shadow="$_shadow $_m"
done
if [ -n "$_shadow" ]; then
    for _m in $_shadow; do
        p_fail "modules/$_m.sh shadows the C module - freeze it at test/ref/${_m}_sh_ref.sh and delete it"
    done
    FAILED=1
else
    p_ok "(every C module owns its name alone)"
fi

if command -v zsh >/dev/null 2>&1; then
    sec "zsh -n (rc.d + theme layers):"
    # Templates too: a placeholder always sits inside a quoted value, so a
    # *.zsh.tmpl is valid zsh before substitution as well as after (§6b).
    for f in $(find "$REPO/zsh" "$OSR_ROOT/themes" \
        -name '*.zsh' -o -name '*.zsh.tmpl' 2>/dev/null); do
        if zsh -n "$f" 2>/dev/null; then
            p_ok "${f#"$REPO"/}"
        else
            p_fail "${f#"$REPO"/}"
            FAILED=1
        fi
    done
else
    p_warn "zsh: not installed - skipping zsh -n"
fi

if [ "$FAILED" -eq 0 ]; then
    printf '%blint: PASS%b\n' "$OSR_GREEN" "$OSR_NC"
else
    printf '%blint: FAIL%b\n' "$OSR_RED" "$OSR_NC" >&2
    exit 1
fi
