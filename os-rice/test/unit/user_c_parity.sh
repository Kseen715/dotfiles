#!/bin/sh
# Proves lib/user.sh (now a shim over `osr user` in the harness core) resolves,
# decides and writes exactly as the pure-sh implementation did, frozen at
# test/ref/user_sh_ref.sh.
#
# Hermetic: no sudo, no real accounts touched. as_user/as_root are stubbed to
# plain exec (they are the one thing that HAS to stay shell, so both sides get
# the same stub), and every file the primitives touch lives in a temp tree.
#
# Compared: the resolved OSR_USER/OSR_HOME over the whole §8 precedence chain,
# the passwd/shell/realpath readers against real accounts on this machine,
# ensure_line and ensure_block over hostile file fixtures (missing file, no
# trailing newline, an existing region, markers that only look like markers),
# backup_copy's three outcomes, and the /etc/passwd rewrite.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
OSR_REF="$OSR_ROOT/test/ref/user_sh_ref.sh"; export OSR_REF
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

# STUBS — as_user/as_root run the command as-is, and error() is log.sh's.
STUBS='as_user() { "$@"; }; as_root() { "$@"; }'
REF_PRE='. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_REF"; '"$STUBS"
NEW_PRE='. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; '"$STUBS"

# run_both <label> <snippet> [env...] — same snippet against both, comparing
# stdout, stderr and exit status.
run_both() {
    _label=$1; _snip=$2
    shift 2
    _rrc=0
    env "$@" sh -c "$REF_PRE; $_snip" >"$TMP/ref.out" 2>"$TMP/ref.err" || _rrc=$?
    _crc=0
    env "$@" sh -c "$NEW_PRE; $_snip" >"$TMP/c.out" 2>"$TMP/c.err" || _crc=$?
    same "$_label: stdout" "$(hex <"$TMP/ref.out")" "$(hex <"$TMP/c.out")"
    same "$_label: stderr" "$(hex <"$TMP/ref.err")" "$(hex <"$TMP/c.err")"
    assert_eq "$_rrc" "$_crc" "$_label: exit status ($_rrc)"
}

# --- 1. the readers, against accounts this machine really has ----------------
ME=$(id -un)
for _u in "$ME" root nosuchuser____; do
    run_both "osr_passwd $_u" "osr_passwd '$_u'"
    run_both "osr_user_shell $_u" "osr_user_shell '$_u'"
done
for _p in /bin/sh /bin/zsh /usr/bin/env "$TMP/nope" "$TMP/no/such/dir/file" /; do
    run_both "osr_realpath $_p" "osr_realpath '$_p'"
done
run_both "osr_shell_is: real shell" "osr_shell_is '$ME' \"\$(osr_user_shell '$ME')\""
run_both "osr_shell_is: wrong shell" "osr_shell_is '$ME' /nope/zsh"
run_both "osr_shell_is: unknown user" "osr_shell_is nosuchuser____ /bin/sh"
run_both "osr_register_shell: already listed" "osr_register_shell /bin/sh; echo rc=\$?"

# --- 2. osr_resolve_user, the whole §8 precedence chain ----------------------
PRINT='osr_resolve_user "$1"; printf "user=[%s] home=[%s]\n" "$OSR_USER" "$OSR_HOME"'
for _case in "explicit:$ME" "explicit-root:root" "explicit-missing:nosuchuser____" "none:"; do
    _want=${_case#*:}
    run_both "resolve ($_case)" "set -- '$_want'; $PRINT" SUDO_USER= USER="$ME"
done
run_both "resolve: SUDO_USER wins over USER" "set -- ''; $PRINT" SUDO_USER="$ME" USER=nobody
run_both "resolve: SUDO_USER=root is ignored" "set -- ''; $PRINT" SUDO_USER=root USER="$ME"
run_both "resolve: neither set" "set -- ''; $PRINT" SUDO_USER= USER=

# --- 3. ensure_line ----------------------------------------------------------
# Fixtures for what grep -qF actually tested: absent, present, present as a
# substring of a longer line, and a file with no trailing newline.
el_fixture() {
    case "$2" in
        missing)   rm -f "$1" ;;
        empty)     : >"$1" ;;
        absent)    printf 'unrelated\nlines here\n' >"$1" ;;
        present)   printf 'unrelated\nexport PATH=/opt/bin:$PATH\nmore\n' >"$1" ;;
        substring) printf '# export PATH=/opt/bin:$PATH (commented)\n' >"$1" ;;
        nonl)      printf 'unrelated\nno trailing newline' >"$1" ;;
    esac
}
for _fx in missing empty absent present substring nonl; do
    el_fixture "$TMP/ref.el" "$_fx"; el_fixture "$TMP/c.el" "$_fx"
    _line='export PATH=/opt/bin:$PATH'
    env sh -c "$REF_PRE"'; ensure_line "$1" "$2"' _ "$TMP/ref.el" "$_line" >/dev/null 2>&1 || :
    env sh -c "$NEW_PRE"'; ensure_line "$1" "$2"' _ "$TMP/c.el" "$_line" >/dev/null 2>&1 || :
    same "ensure_line ($_fx)" "$(hex <"$TMP/ref.el" 2>/dev/null)" "$(hex <"$TMP/c.el" 2>/dev/null)"
    # ...and again: the second run must change nothing (§2).
    env sh -c "$REF_PRE"'; ensure_line "$1" "$2"' _ "$TMP/ref.el" "$_line" >/dev/null 2>&1 || :
    env sh -c "$NEW_PRE"'; ensure_line "$1" "$2"' _ "$TMP/c.el" "$_line" >/dev/null 2>&1 || :
    same "ensure_line ($_fx, rerun)" "$(hex <"$TMP/ref.el")" "$(hex <"$TMP/c.el")"
done

# --- 4. ensure_block ---------------------------------------------------------
eb_fixture() {
    case "$2" in
        missing)  rm -f "$1" ;;
        plain)    printf 'before\nafter\n' >"$1" ;;
        owned)    printf 'before\n# >>> os-rice:loader >>>\nOLD BODY\nstale\n# <<< os-rice:loader <<<\nafter\n' >"$1" ;;
        twice)    printf '# >>> os-rice:loader >>>\nA\n# <<< os-rice:loader <<<\nmid\n# >>> os-rice:loader >>>\nB\n# <<< os-rice:loader <<<\n' >"$1" ;;
        lookalike) printf '  # >>> os-rice:loader >>>\nindented marker is not a marker\n' >"$1" ;;
        othername) printf '# >>> os-rice:other >>>\nX\n# <<< os-rice:other <<<\n' >"$1" ;;
        nonl)     printf 'before\nno newline at eof' >"$1" ;;
    esac
}
BODY='for f in "$HOME"/.config/osr/zsh/rc.d/*.zsh; do . "$f"; done'
for _fx in missing plain owned twice lookalike othername nonl; do
    eb_fixture "$TMP/ref.eb" "$_fx"; eb_fixture "$TMP/c.eb" "$_fx"
    printf '%s\n' "$BODY" | env sh -c "$REF_PRE"'; ensure_block "$1" loader' _ "$TMP/ref.eb" >/dev/null 2>&1 || :
    printf '%s\n' "$BODY" | env sh -c "$NEW_PRE"'; ensure_block "$1" loader' _ "$TMP/c.eb" >/dev/null 2>&1 || :
    same "ensure_block ($_fx)" "$(hex <"$TMP/ref.eb")" "$(hex <"$TMP/c.eb")"
    printf '%s\n' "$BODY" | env sh -c "$REF_PRE"'; ensure_block "$1" loader' _ "$TMP/ref.eb" >/dev/null 2>&1 || :
    printf '%s\n' "$BODY" | env sh -c "$NEW_PRE"'; ensure_block "$1" loader' _ "$TMP/c.eb" >/dev/null 2>&1 || :
    same "ensure_block ($_fx, rerun is idempotent)" "$(hex <"$TMP/ref.eb")" "$(hex <"$TMP/c.eb")"
done

# --- 5. backup_copy ----------------------------------------------------------
bc_setup() {
    rm -rf "$1"; mkdir -p "$1"
    printf 'new content\n' >"$1/src"
    case "$2" in
        fresh)    ;;
        differs)  printf 'old content\n' >"$1/dst" ;;
        same)     printf 'new content\n' >"$1/dst" ;;
        hasbak)   printf 'old content\n' >"$1/dst"; printf 'older\n' >"$1/dst.bak" ;;
        deep)     ;;
    esac
}
for _fx in fresh differs same hasbak deep; do
    bc_setup "$TMP/refbc" "$_fx"; bc_setup "$TMP/cbc" "$_fx"
    _dst=dst; [ "$_fx" = deep ] && _dst=sub/dir/dst
    env sh -c "$REF_PRE"'; backup_copy "$1/src" "$1/'"$_dst"'"' _ "$TMP/refbc" >/dev/null 2>&1 || :
    env sh -c "$NEW_PRE"'; backup_copy "$1/src" "$1/'"$_dst"'"' _ "$TMP/cbc" >/dev/null 2>&1 || :
    same "backup_copy ($_fx): tree" \
        "$(cd "$TMP/refbc" && find . -type f | sort | while read -r f; do printf '%s:' "$f"; cat "$f"; done | hex)" \
        "$(cd "$TMP/cbc" && find . -type f | sort | while read -r f; do printf '%s:' "$f"; cat "$f"; done | hex)"
done
run_both "backup_copy: missing source is fatal" "backup_copy '$TMP/nope' '$TMP/out'"

# --- 6. the /etc/passwd rewrite ----------------------------------------------
# The real one is never touched: the composed content is compared instead, which
# is exactly what the sh version wrote through `as_root cp`.
printf 'root:x:0:0:root:/root:/bin/bash\n%s:x:1000:1000:me:/home/%s:/bin/sh\nsvc:x:999:999::/:/usr/sbin/nologin\n' "$ME" "$ME" >"$TMP/passwd"
_r=$(awk -F: -v OFS=: -v u="$ME" -v s=/usr/bin/zsh '$1 == u { $7 = s } { print }' "$TMP/passwd" | hex)
_c=$(OSR_PASSWD_FILE="$TMP/passwd" "$OSR_BIN" user passwd-shell-file "$TMP/passwd" "$ME" /usr/bin/zsh 2>/dev/null | hex)
same "passwd-shell: field 7 rewritten, rest verbatim" "$_r" "$_c"
_c_missing=0
"$OSR_BIN" user passwd-shell-file "$TMP/passwd" nosuchuser____ /usr/bin/zsh >/dev/null 2>&1 || _c_missing=$?
assert_eq 1 "$_c_missing" "passwd-shell: an unknown account is left alone (exit 1)"

finish
