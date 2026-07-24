# modules/fastfetch.sh — fastfetch system info tool. ONE copy, POSIX,
# distro-agnostic. "Easiest method per distro" is expressed entirely in the
# pkgmap: native package on arch/fedora/void/alpine/gentoo (bare passthrough),
# and the official prebuilt .deb on Debian/Ubuntu (apt.map -> build_fastfetch_deb),
# where fastfetch is packaged natively only on very recent releases.
#
# https://github.com/fastfetch-cli/fastfetch

run_step "Installing fastfetch" pkg_install fastfetch
