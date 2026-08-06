# session: x11+wayland
# modules/thunar.sh — GTK file manager (i3-sugg §8.1). The lightest full file
# manager that still has a devices sidebar, trash, and thumbnails, which is
# exactly the set that depends on modules/gvfs.sh + modules/thumbnails.sh being
# installed first (manifest order is the dependency graph, §4).
#
# Void capitalises the package (Thunar); xbps.map carries the row.
# thunar-volman handles removable-media actions, the archive plugin adds
# "Extract here" via xarchiver.

run_step "Installing Thunar" pkg_install \
    thunar thunar-volman thunar-archive-plugin thunar-media-tags-plugin tumbler

# Thunar reads GTK settings, so its theming comes from modules/theming.sh. It
# does need a running daemon for the file picker to be fast — the i3 config
# execs `thunar --daemon`.
