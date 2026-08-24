# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
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
