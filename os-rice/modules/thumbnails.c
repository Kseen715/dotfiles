/* modules/thumbnails.c -- thumbnailers + image loaders (i3-sugg §3.6).
 * tumbler is the D-Bus thumbnail service (from XFCE, but it works standalone and
 * is what every GTK file manager and file picker asks). The rest are the format
 * handlers; each missing one is a category of blank icons.
 *
 * The pixbuf loaders matter beyond thumbnails: without them WebP/AVIF/HEIC/JXL
 * render as blank in image viewers, the browser download panel, and GTK itself.
 * gdk-pixbuf keeps a cache of installed loaders; a package manager usually
 * regenerates it, but a from-source loader would not (best-effort, §9).
 *
 * Port of modules/thumbnails.sh, kept as the reference at
 * test/ref/thumbnails_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_thumbnails(void) {
    static const char *const thumbs[] = {
        "tumbler", "ffmpegthumbnailer", "poppler-glib", "libgsf", "libopenraw",
        "raw-thumbnailer", "gnome-epub-thumbnailer", NULL
    };
    static const char *const loaders[] = {
        "gdk-pixbuf2", "webp-pixbuf-loader", "librsvg", "libheif", "libavif",
        "libjxl", NULL
    };
    char *argv[4];
    int ok;

    ok = osr_pkg_install_step("Installing thumbnailers", thumbs);
    ok = osr_pkg_install_step("Installing image loaders", loaders) && ok;
    /* The loader cache is what makes a newly installed pixbuf loader visible to
     * anything already running. Best effort: a distro that regenerates it from
     * a package hook has nothing for this to do. */
    if (osr_have_cmd("gdk-pixbuf-query-loaders")) {
        argv[0] = (char *)"sh"; argv[1] = (char *)"-c";
        argv[2] = (char *)"gdk-pixbuf-query-loaders --update-cache";
        argv[3] = NULL;
        (void)osr_run_root_quiet(argv);
    }
    return ok;
}
