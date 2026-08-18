#!/bin/sh
# Proves `osr test-run` (lib/testrun.c, in the harness core) drives the
# suite exactly as the pure-sh runner did, frozen at test/ref/run_sh_ref.sh.
# test/run.sh itself is gone: it had nothing left to do that the core cannot,
# so ./osr calls the core directly and this compares the two.
#
# Both runners are pointed at a FIXTURE tree - a lib/ symlink, a stub lint.sh
# and a handful of stub unit tests - so the comparison is deterministic and
# does not recurse into the real suite. What is compared: stdout, stderr and
# the exit status, over a passing tree, a tree with a failing test, a tree
# whose lint fails, and an empty tree. One stub test echoes $OSR_TEST_COLOR,
# which is the one variable the runner has to export for test/lib.sh.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

# The two trees sit at different paths, and sh names the file it could not
# open in its diagnostics, so the tree root is normalized away before the
# byte comparison - everything else must still match exactly.
hex() { sed "s|$TMP/ref|TREE|g; s|$TMP/c|TREE|g" | od -An -tx1 | tr -d ' \n'; }

same() {
    if [ "$2" = "$3" ]; then
        ok "$1"
    else
        fail "$1"
        printf '    ref: %s\n    c  : %s\n' "$2" "$3" >&2
    fi
}

# make_tree <dir> <lint-rc> <kind> — a miniature os-rice tree the runners can
# be pointed at: lib/ is the real one (both runners source ../lib/ui.sh from
# their own location), test/ holds the stubs.
make_tree() {
    _t=$1; _lint_rc=$2; _kind=$3
    mkdir -p "$_t/test/unit"
    ln -sfn "$OSR_LIB" "$_t/lib"
    cp "$OSR_ROOT/test/ref/run_sh_ref.sh" "$_t/test/run_ref.sh"
    # ./osr resolves the core through lib/ui.sh and then runs it; the fixture
    # gets the same two, so both sides source the same ui.sh.
    ln -sfn "$OSR_ROOT/build" "$_t/build"
    printf '%s\n' 'set -eu' \
        'HERE=$(cd -- "$(dirname -- "$0")" && pwd)' \
        '. "$HERE/../lib/ui.sh"' \
        '"$OSR_BIN" test-run "$HERE"' >"$_t/test/run_core.sh"
    cat >"$_t/test/lint.sh" <<EOF
printf 'stub lint: checking\n'
printf 'stub lint: a warning\n' >&2
exit $_lint_rc
EOF
    case "$_kind" in
        pass|lintfail)
            printf 'printf "  ok   stub one\\n"\n' >"$_t/test/unit/aaa_first.sh"
            printf 'printf "  color=[%%s]\\n" "$OSR_TEST_COLOR"\n' >"$_t/test/unit/bbb_color.sh"
            printf 'printf "  ok   stub three\\n"\n' >"$_t/test/unit/ccc_last.sh"
            ;;
        withfail)
            printf 'printf "  ok   stub one\\n"\n' >"$_t/test/unit/aaa_first.sh"
            printf 'printf "  FAIL stub two\\n" >&2\nexit 1\n' >"$_t/test/unit/bbb_broken.sh"
            printf 'printf "  ok   stub three\\n"\n' >"$_t/test/unit/ccc_last.sh"
            ;;
        empty) ;;
    esac
}

# Both runners must see the same environment. OSR_LIB must NOT leak in: each
# runner resolves lib/ from its own $0, which is what the fixture tree tests.
BASE_ENV='OSR_LIB= OSR_BIN= OSR_ROOT= TERM=dumb COLUMNS= NO_COLOR=1'

for _case in 'pass 0' 'withfail 0' 'lintfail 1' 'empty 0'; do
    # shellcheck disable=SC2086  # deliberate split into "kind lint-rc"
    set -- $_case
    _kind=$1; _lint=$2
    rm -rf "$TMP/ref" "$TMP/c"
    make_tree "$TMP/ref" "$_lint" "$_kind"
    make_tree "$TMP/c" "$_lint" "$_kind"

    _rrc=0
    # shellcheck disable=SC2086
    env $BASE_ENV sh "$TMP/ref/test/run_ref.sh" >"$TMP/ref.out" 2>"$TMP/ref.err" || _rrc=$?
    _crc=0
    # shellcheck disable=SC2086
    env $BASE_ENV sh "$TMP/c/test/run_core.sh" >"$TMP/c.out" 2>"$TMP/c.err" || _crc=$?

    same "$_kind: stdout bytes" "$(hex <"$TMP/ref.out")" "$(hex <"$TMP/c.out")"
    same "$_kind: stderr bytes" "$(hex <"$TMP/ref.err")" "$(hex <"$TMP/c.err")"
    assert_eq "$_rrc" "$_crc" "$_kind: same exit status ($_rrc)"
done

# --- on a terminal -----------------------------------------------------------
# The palette is only non-empty on a tty, and OSR_TEST_COLOR is derived from
# it, so the colored path needs a pty to be exercised at all.
if command -v script >/dev/null 2>&1; then
    rm -rf "$TMP/ref" "$TMP/c"
    make_tree "$TMP/ref" 0 pass
    make_tree "$TMP/c" 0 pass
    _r=$(script -q -c "env $BASE_ENV NO_COLOR= sh $TMP/ref/test/run_ref.sh" /dev/null | hex)
    _c=$(script -q -c "env $BASE_ENV NO_COLOR= sh $TMP/c/test/run_core.sh" /dev/null | hex)
    same "pty: colored output and OSR_TEST_COLOR=1" "$_r" "$_c"
else
    ok "pty check skipped (no script(1))"
fi

finish
