# test/ref/steam_sh_ref.sh — the sh implementation of modules/steam.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/steam.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/steam.sh — Steam (native, from the [multilib] repo — enable it with the
# pacman-multilib module first). POSIX port of .../apps/steam.sh. Adds the
# Wayland-scaling env var to the user's .bashrc (idempotent via ensure_line) and,
# when systemd-resolved is in use, the resolv.conf symlink Steam expects.
run_step "Installing Steam" pkg_install steam ttf-liberation lib32-systemd

ensure_line "$OSR_HOME/.bashrc" 'export STEAM_FORCE_DESKTOPUI_SCALING=1'
as_user mkdir -p "$OSR_HOME/.local/share/Steam"

# systemd-resolved stub symlink (real-host concern; guarded + idempotent).
if [ -d /run/systemd/resolve ] && [ ! -L /etc/resolv.conf ]; then
    as_root ln -sf ../run/systemd/resolve/stub-resolv.conf /etc/resolv.conf
fi
