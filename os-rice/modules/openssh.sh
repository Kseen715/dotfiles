# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/openssh.sh — OpenSSH client/server + sshd enabled. ONE copy, POSIX
# (was .../modules/openssh.sh). Service control goes through enable_service so it
# works on any init (§8), not just systemd's systemctl.
run_step "Installing OpenSSH" pkg_install openssh
enable_service sshd
