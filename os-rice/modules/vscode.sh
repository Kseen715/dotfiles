# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/vscode.sh — VS Code, the distro-packaged build (Void ships `vscode`,
# Arch `code`; the MS-branded Insiders channel is the Arch-only sibling module
# `vscode-insiders`). Never list both in one rice.
#
# Deliberately NOT config-managed. VS Code has its own first-class settings sync
# (Settings Sync, signed into a GitHub/Microsoft account) plus a profiles system,
# and both write the same settings.json that os-rice would own. Two managers on
# one file means whichever ran last wins and the other silently loses edits — so
# this module installs the editor and stops there.
#
# That includes the theme: pick it in VS Code (or let Settings Sync carry it),
# not here. The rice ships no VS Code palette on purpose.

run_step "Installing VS Code" pkg_install vscode
