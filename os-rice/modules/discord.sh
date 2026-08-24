# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/discord.sh — Discord. ONE copy, POSIX (was .../apps/discord.sh).
run_step "Installing Discord" pkg_install discord
