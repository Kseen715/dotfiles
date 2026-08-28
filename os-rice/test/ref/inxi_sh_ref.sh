# test/ref/inxi_sh_ref.sh — the sh implementation of modules/inxi.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/inxi.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/inxi.sh — inxi system information tool. ONE copy, POSIX,
# distro-agnostic (was linux-debian/modules/inxi.sh). Native on every target.

run_step "Installing inxi" pkg_install inxi
