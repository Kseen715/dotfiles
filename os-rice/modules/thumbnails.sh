# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/thumbnails.sh — thumbnailers + image loaders (i3-sugg §3.6).
# tumbler is the D-Bus thumbnail service (from XFCE, but it works standalone and
# is what every GTK file manager and file picker asks). The rest are the format
# handlers; each missing one is a category of blank icons.
#
# The pixbuf loaders matter beyond thumbnails: without them WebP/AVIF/HEIC/JXL
# render as blank in image viewers, the browser download panel, and GTK itself.

run_step "Installing thumbnailers" pkg_install \
    tumbler ffmpegthumbnailer poppler-glib libgsf libopenraw raw-thumbnailer \
    gnome-epub-thumbnailer

run_step "Installing image loaders" pkg_install \
    gdk-pixbuf2 webp-pixbuf-loader librsvg libheif libavif libjxl

# gdk-pixbuf keeps a cache of installed loaders; a package manager usually
# regenerates it, but a from-source loader would not (best-effort, §9).
command -v gdk-pixbuf-query-loaders >/dev/null 2>&1 \
    && as_root sh -c 'gdk-pixbuf-query-loaders --update-cache' >/dev/null 2>&1 || :
