# test/ref/thunar_sh_ref.sh — the sh implementation of modules/thunar.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/thunar.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
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

# "Open Terminal Here" needs exo and a role -> helper mapping, which is
# modules/helpers.c (a C module) - list it in the rice, not here: it is not
# Thunar-specific, every exo-using app resolves the same roles through it.

# Thunar reads GTK settings, so its theming comes from modules/theming.sh. It
# does need a running daemon for the file picker to be fast — the i3 config
# execs `thunar --daemon`.
