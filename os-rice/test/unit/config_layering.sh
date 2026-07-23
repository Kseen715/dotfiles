#!/bin/sh
# Proves §5: seed_once keeps user edits, install_layer overwrites+backs-up once,
# the loader block is idempotent and preserves the user's own .zshrc.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_DOTFILES OSR_PKG=apt
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"
. "$HERE/../lib.sh"

H=$(mktemp -d); OSR_HOME="$H"; export OSR_HOME
RC="$OSR_HOME/.config/osr/zsh/rc.d"

seed_once "$OSR_DOTFILES/zsh/rc.d/00-env.zsh" "$RC/00-env.zsh" >/dev/null
echo "USER-EDIT" >> "$RC/00-env.zsh"
seed_once "$OSR_DOTFILES/zsh/rc.d/00-env.zsh" "$RC/00-env.zsh" >/dev/null
assert_contains "$RC/00-env.zsh" "USER-EDIT" "seed_once preserves user edit"

printf 'OLD\n' > "$RC/10-omz.zsh"
install_layer "$OSR_DOTFILES/zsh/rc.d/10-omz.zsh" "$RC/10-omz.zsh" >/dev/null
assert_contains "$RC/10-omz.zsh.bak" "OLD" "install_layer backs up once"
assert_contains "$RC/10-omz.zsh" "oh-my-zsh bootstrap" "install_layer overwrites"

printf '# my zshrc\nexport FOO=1\n' > "$OSR_HOME/.zshrc"
install_zsh_loader "$RC" "$OSR_HOME/.zshrc" >/dev/null
install_zsh_loader "$RC" "$OSR_HOME/.zshrc" >/dev/null
n=$(grep -c 'os-rice:loader' "$OSR_HOME/.zshrc")
assert_eq 2 "$n" "loader block not duplicated on rerun"
assert_contains "$OSR_HOME/.zshrc" "export FOO=1" "user .zshrc content preserved"

rm -rf "$H"
finish
