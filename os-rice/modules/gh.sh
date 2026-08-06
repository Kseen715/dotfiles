# session: x11+wayland
# modules/gh.sh — GitHub CLI. ONE copy, POSIX, distro-agnostic (was
# linux-debian/modules/gh.sh). Native-first: the package is `github-cli` on
# arch/alpine/void and `gh` on fedora/Debian/Ubuntu (resolved by pkgmap). Only
# Debian 11 (bullseye) lacks it -> upstream release tarball via an apt.map row.

run_step "Installing GitHub CLI" pkg_install gh git
