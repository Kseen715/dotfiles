#!/bin/sh
# os-rice — single shared installer.  Usage: install.sh [options] <rice>
#
#   install.sh gruvbox                 rice OSR_USER (auto-resolved)
#   install.sh --user alice gruvbox    rice a specific user (user-for-user, §8)
#   install.sh --verbose gruvbox       stream output, no spinners
#   install.sh --module zsh foot       install specific module(s), no rice
#   install.sh --theme nord gruvbox    install a rice painted with another theme
#   install.sh --theme-only --theme nord  apply a theme only (the hotkey path)
#   install.sh --list                  list available rices
#   install.sh --list-modules          list available modules
#
# This file is a shim. The installer itself is `osr install run` in the harness
# core (lib/install.c): the option loop, the manifest, the detected-facts
# report, the theme resolution, the module loop and the closing line.
#
# It was the last unit to stay shell, and for one reason: it SOURCED each
# module, and only a shell can source a shell script into itself. No module is
# a shell script any more (DESIGN §11a), so the orchestration moved into the
# core and what is left here is the entry point people and scripts already
# type. Byte-for-byte the sh original, frozen at test/ref/install_sh_ref.sh and
# diffed by test/unit/install_c_parity.sh.
#
# A `.sh` module that appears again still runs: the core hands it to a shell
# with the libs sourced around it, which is the same command this file ran.
set -eu

OSR_ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_LIB="$OSR_ROOT/lib"
# The dotfiles repo root is the parent of os-rice/ — configs live there.
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
export OSR_ROOT OSR_LIB OSR_DOTFILES

# ui.sh is sourced for one line: the OSR_BIN it resolves (and builds, on a
# checkout that has never been built). Everything after that is the binary.
. "$OSR_LIB/ui.sh"

exec "$OSR_BIN" install run "$@"
