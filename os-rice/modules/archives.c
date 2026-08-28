/* modules/archives.c -- archive formats + a GUI to open them (i3-sugg §8.3).
 * thunar-archive-plugin's "Extract Here" is a front-end: it calls whichever GUI
 * archiver is installed, and the format support comes from the CLI backends
 * below. Install the plugin without the backends and right-click extraction
 * fails on exactly the formats people actually download (.7z, .rar).
 *
 * xarchiver is the light GTK pick; file-roller is the GNOME one and is installed
 * too because it is what many .desktop entries name directly.
 *
 * Port of modules/archives.sh, kept as the reference at
 * test/ref/archives_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_archives(void) {
    static const char *const guis[] = { "xarchiver", "file-roller", NULL };
    static const char *const formats[] = {
        "p7zip", "unrar", "unzip", "zip", "tar", "xz", "zstd", "lzip",
        "cabextract", "atool", "ouch", NULL
    };
    int ok;

    ok = osr_pkg_install_step("Installing archive GUIs", guis);
    return osr_pkg_install_step("Installing archive formats", formats) && ok;
}
