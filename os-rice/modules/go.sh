# session: x11+wayland
# modules/go.sh — Go toolchain. ONE copy, POSIX, distro-agnostic (was
# linux-debian/modules/go.sh, which fetched go.dev tarballs). Native-first: the
# distro package is used everywhere (updatable via the package manager). The
# name differs (dnf/apt call it `golang`, others `go`), resolved by pkgmap.

run_step "Installing Go" pkg_install go
