# test/ref/git-base_sh_ref.sh — the sh implementation of modules/git-base.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/git-base.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/git-base.sh — git + core CLI tools (git, wget, editors, man). The base
# every later module and the user relies on. Native everywhere; ONE copy, POSIX
# (was linux-arch-x86_64-hyprland-glass/modules/git.sh, bash+pacman).
run_step "Installing git and base CLI tools" pkg_install git wget nano vim man-db
