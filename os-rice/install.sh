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
# This file is a shim over `osr install`, which is a shim over `osr install
# run` -- the installer itself, in the harness core (lib/install.c): the option
# loop, the manifest, the detected-facts report, the theme resolution, the
# module loop and the closing line.
#
# It was the last unit to stay shell, and for one reason: it SOURCED each
# module, and only a shell can source a shell script into itself. No module is
# a shell script any more (DESIGN §11a), so the orchestration moved into the
# core and what is left here is the entry point people and scripts already
# type.
#
# A `.sh` module that appears again still runs: the core hands it to a shell
# with the libs sourced around it, which is the same command this file ran.
set -eu

# Delegates to ./osr rather than resolving the binary itself. Locating (and on
# a fresh checkout building) build/osr is the one thing the shell tier still
# has to do, and it is worth doing in exactly one file -- `osr install` is the
# same engine this entry point named, so this stays a name people can type
# without becoming a second copy of the bootstrap.
exec "$(cd -- "$(dirname -- "$0")" && pwd)/osr" install "$@"
