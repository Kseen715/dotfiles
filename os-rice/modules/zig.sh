# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/zig.sh — Zig toolchain. ONE copy, POSIX, distro-agnostic (was
# linux-debian/modules/zig.sh, which added the debian.griffo.io apt repo).
# Native-first: native on arch/fedora/alpine/void and recent Ubuntu; Debian and
# older Ubuntu get the official ziglang.org tarball (source:provide_zig via apt.map).

run_step "Installing Zig" pkg_install zig
