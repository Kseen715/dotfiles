# modules/ghostty.sh — Ghostty terminal. ONE copy, POSIX, distro-agnostic (was
# linux-debian/modules/ghostty.sh, a from-source Zig build). Native-first: native
# on arch/void and recent Ubuntu; elsewhere built from source with a bootstrapped
# Zig toolchain (source:build_ghostty via pkgmap). The source build is heavy (a
# full Zig compile) and is a real-desktop concern (§9), not container-tested.

run_step "Installing Ghostty" pkg_install ghostty
