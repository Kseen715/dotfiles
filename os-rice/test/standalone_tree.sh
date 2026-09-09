#!/bin/sh
# Integration test for the standalone binary: a copy of build/osr run from a
# directory with no checkout around it, which is how the released artifact is
# used (download one file, run it).
#
# Two things have to happen by themselves there, and neither did before:
# the facts get detected (OSR_PKG='' was fatal in lib/pkg.c), and the tree the
# data files live in gets fetched (themes/, lib/pkgmap, the dotfiles configs a
# module copies). The clone source here is the checkout this test runs from,
# so nothing reaches the network.
set -eu

ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
DOTFILES=$(cd -- "$ROOT/.." && pwd)
TMP=${TMPDIR:-/tmp}/osr-standalone-test.$$
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

mkdir -p "$TMP/wd" "$TMP/home"
cp "$ROOT/build/osr" "$TMP/wd/osr"
BIN="$TMP/wd/osr"

run_standalone() {
    _dest=$1; shift
    env -u OSR_ROOT -u OSR_LIB -u OSR_DOTFILES -u OSR_PKG \
        HOME="$TMP/home" OSR_HOME="$TMP/home" OSR_USER="$(id -un)" \
        OSR_DEST="$_dest" OSR_REPO_URL="$DOTFILES" NO_COLOR=1 \
        "$BIN" "$@"
}

# A command that reads the checkout fetches one, and then answers out of it.
run_standalone "$TMP/tree" theme list >"$TMP/out" 2>&1
grep -q gruvbox "$TMP/out"
[ -d "$TMP/tree/os-rice/themes" ]

# A second run reuses what is already there: no fetch, so this works offline
# and costs nothing.
run_standalone "$TMP/tree" theme list >"$TMP/out" 2>&1
grep -q gruvbox "$TMP/out"
if grep -q 'no os-rice tree' "$TMP/out"; then
    printf 'second run fetched again\n' >&2
    exit 1
fi

# A command that reads nothing from the checkout never reaches for one --
# `osr log info` must not clone, on a metered connection least of all.
run_standalone "$TMP/none" log info hi >"$TMP/out" 2>&1
grep -q 'hi' "$TMP/out"
if [ -e "$TMP/none" ]; then
    printf 'log fetched a tree it does not read\n' >&2
    exit 1
fi

# The facts detect themselves on first use: with nothing exported, package
# resolution used to die with "no native installer for OSR_PKG=''".
run_standalone "$TMP/tree" module pkgmap zsh >"$TMP/out" 2>&1
if grep -q "OSR_PKG=''" "$TMP/out"; then
    printf 'package manager was not detected\n' >&2
    exit 1
fi
[ -s "$TMP/out" ]

printf 'standalone tree tests passed\n'
