# test/ref/vscode-insiders_sh_ref.sh — the sh implementation of modules/vscode-insiders.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/vscode-insiders.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/vscode-insiders.sh — VS Code Insiders (AUR) + coding fonts. ONE copy,
# POSIX (was .../apps/vscode-insiders.sh). Maps vscode-insiders ->
# aur:visual-studio-code-insiders-bin.
run_step "Installing VS Code Insiders (AUR)" pkg_install vscode-insiders
run_step "Installing coding fonts" pkg_install \
    ttf-cascadia-code-nerd ttf-cascadia-mono-nerd ttf-iosevkaterm-nerd
