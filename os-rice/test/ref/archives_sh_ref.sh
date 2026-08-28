# test/ref/archives_sh_ref.sh — the sh implementation of modules/archives.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/archives.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/archives.sh — archive formats + a GUI to open them (i3-sugg §8.3).
# thunar-archive-plugin's "Extract Here" is a front-end: it calls whichever GUI
# archiver is installed, and the format support comes from the CLI backends
# below. Install the plugin without the backends and right-click extraction
# fails on exactly the formats people actually download (.7z, .rar).
#
# xarchiver is the light GTK pick; file-roller is the GNOME one and is installed
# too because it is what many .desktop entries name directly.

run_step "Installing archive GUIs" pkg_install xarchiver file-roller

run_step "Installing archive formats" pkg_install \
    p7zip unrar unzip zip tar xz zstd lzip cabextract atool ouch
