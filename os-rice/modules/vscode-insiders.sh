# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/vscode-insiders.sh — VS Code Insiders (AUR) + coding fonts. ONE copy,
# POSIX (was .../apps/vscode-insiders.sh). Maps vscode-insiders ->
# aur:visual-studio-code-insiders-bin.
run_step "Installing VS Code Insiders (AUR)" pkg_install vscode-insiders
run_step "Installing coding fonts" pkg_install \
    ttf-cascadia-code-nerd ttf-cascadia-mono-nerd ttf-iosevkaterm-nerd
