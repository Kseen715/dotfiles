#!/bin/sh
# Proves §5 guard-style PATH: sourcing 00-env.zsh twice never duplicates an
# entry (the re-run contract for env, acceptance "no duplicated PATH entries").
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
. "$HERE/../lib.sh"

H=$(mktemp -d); HOME="$H"; export HOME
mkdir -p "$H/.cargo/bin"          # a dir the guard will add
PATH="/usr/bin:/bin"; export PATH

. "$OSR_DOTFILES/zsh/rc.d/00-env.zsh"
. "$OSR_DOTFILES/zsh/rc.d/00-env.zsh"   # second source must not duplicate

n=$(printf '%s' "$PATH" | tr ':' '\n' | grep -cx "$H/.cargo/bin" || true)
assert_eq 1 "$n" "cargo/bin appears exactly once after double-source"

rm -rf "$H"
finish
