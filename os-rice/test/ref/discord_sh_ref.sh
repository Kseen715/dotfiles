# test/ref/discord_sh_ref.sh — the sh implementation of modules/discord.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/discord.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/discord.sh — Discord. ONE copy, POSIX (was .../apps/discord.sh).
run_step "Installing Discord" pkg_install discord
