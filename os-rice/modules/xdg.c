/* modules/xdg.c -- the XDG layer: portals, user dirs, MIME, desktop entries
 * (i3-sugg §3.2 + §3.4). This is the module that decides whether "apps just
 * work" or fail in ways nobody connects back to the WM:
 *
 * no portal            Flatpak/Chromium/Electron file dialogs are blank
 * no XDG_CURRENT_DESKTOP  the portal cannot pick a backend at all
 * no xdg-user-dirs     browsers have no ~/Downloads to save into
 * no shared-mime-info  every file is application/octet-stream
 * no desktop-file-utils  "Open With" is empty
 *
 * i3 ships no portal backend of its own, so the gtk one is pinned explicitly.
 * XDG_CURRENT_DESKTOP=i3 is exported by the xprofile layer (modules/xorg.sh).
 * Pin the backend: i3 is not a desktop the portal knows, so without this it
 * either picks nothing or picks whatever happens to be installed.
 * Managed by os-rice (modules/xdg.sh) — i3 has no portal backend, so pin gtk.
 * Create ~/Downloads, ~/Pictures, ... once. Idempotent by design; the databases
 * below are best-effort (a fresh container has nothing to index).
 * Default applications. Seeded once — which browser opens a link is the user's
 * call, and rewriting it on every rice switch would be obnoxious.
 *
 * Was modules/xdg.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"

#include <stddef.h>

int osrm_xdg(void) {
    static const char *const pkgs[] = {
        "xdg-desktop-portal", "xdg-desktop-portal-gtk", "xdg-utils",
        "xdg-user-dirs", "xdg-user-dirs-gtk", "shared-mime-info",
        "desktop-file-utils", "hicolor-icon-theme", NULL
    };
    /* A bare WM has no portal implementation of its own, so the GTK one is
     * named explicitly - without this the Settings portal has no backend and
     * every app falls back to its own idea of the colour scheme. */
    static const char portals[] =
        "[preferred]\n"
        "default=gtk\n"
        "org.freedesktop.impl.portal.Settings=gtk\n";
    Str dir, dst, src;
    char *argv[4];
    int ok;

    ok = osr_pkg_install_step("Installing XDG portals + basics", pkgs);

    str_init(&dir); str_init(&dst); str_init(&src);
    str_addz(&dir, osr_mod_home()); str_addz(&dir, "/.config/xdg-desktop-portal");
    ok = osr_mkdir_p(str_text(&dir)) && ok;
    str_addz(&dst, str_text(&dir)); str_addz(&dst, "/i3-portals.conf");
    ok = osr_write_user(str_text(&dst), portals) && ok;

    argv[0] = (char *)"xdg-user-dirs-update"; argv[1] = NULL;
    (void)osr_run_step_user("Creating XDG user dirs", argv);

    /* Both caches are best-effort: a distro that regenerates them from a
     * package hook has nothing for these to do. */
    if (osr_have_cmd("update-mime-database")) {
        argv[0] = (char *)"update-mime-database"; argv[1] = (char *)"/usr/share/mime";
        argv[2] = NULL;
        (void)osr_run_root_quiet(argv);
    }
    if (osr_have_cmd("update-desktop-database")) {
        argv[0] = (char *)"update-desktop-database";
        argv[1] = (char *)"/usr/share/applications"; argv[2] = NULL;
        (void)osr_run_root_quiet(argv);
    }
    /* Seeded, not owned: which app opens what is the user's to change. */
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/xdg/mimeapps.list");
    str_reset(&dst);
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.config/mimeapps.list");
    if (file_exists(str_text(&src)))
        ok = osr_seed_once(str_text(&src), str_text(&dst)) && ok;

    str_free(&dir); str_free(&dst); str_free(&src);
    return ok;
}
