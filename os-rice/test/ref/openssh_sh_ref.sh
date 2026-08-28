# test/ref/openssh_sh_ref.sh — the sh implementation of modules/openssh.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/openssh.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/openssh.sh — OpenSSH client/server + sshd enabled. ONE copy, POSIX
# (was .../modules/openssh.sh). Service control goes through enable_service so it
# works on any init (§8), not just systemd's systemctl.
run_step "Installing OpenSSH" pkg_install openssh
enable_service sshd
