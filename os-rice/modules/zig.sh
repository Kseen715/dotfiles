# modules/zig.sh — Zig toolchain. ONE copy, POSIX, distro-agnostic (was
# linux-debian/modules/zig.sh, which added the debian.griffo.io apt repo).
# Native-first: native on arch/fedora/alpine/void and recent Ubuntu; Debian and
# older Ubuntu get the official ziglang.org tarball (source:build_zig via apt.map).

run_step "Installing Zig" pkg_install zig
