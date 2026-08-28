# test/ref/go_sh_ref.sh — the sh implementation of modules/go.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/go.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/go.sh — Go toolchain. ONE copy, POSIX, distro-agnostic (was
# linux-debian/modules/go.sh, which fetched go.dev tarballs). Native-first: the
# distro package is used everywhere (updatable via the package manager). The
# name differs (dnf/apt call it `golang`, others `go`), resolved by pkgmap.

run_step "Installing Go" pkg_install go
