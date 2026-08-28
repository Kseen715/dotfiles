# test/ref/zig_sh_ref.sh — the sh implementation of modules/zig.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/zig.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/zig.sh — Zig toolchain. ONE copy, POSIX, distro-agnostic (was
# linux-debian/modules/zig.sh, which added the debian.griffo.io apt repo).
# Native-first: native on arch/fedora/alpine/void and recent Ubuntu; Debian and
# older Ubuntu get the official ziglang.org tarball (source:provide_zig via apt.map).

run_step "Installing Zig" pkg_install zig
