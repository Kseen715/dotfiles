/* modules/steam.c -- Steam (native, from the [multilib] repo — enable it with the
 * pacman-multilib module first). POSIX port of .../apps/steam.sh. Adds the
 * Wayland-scaling env var to the user's .bashrc (idempotent via ensure_line) and,
 * when systemd-resolved is in use, the resolv.conf symlink Steam expects.
 * systemd-resolved stub symlink (real-host concern; guarded + idempotent).
 *
 * Port of modules/steam.sh, kept as the reference at
 * test/ref/steam_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <sys/stat.h>

int osrm_steam(void) {
    static const char *const pkgs[] = { "steam", "ttf-liberation", "lib32-systemd", NULL };
    Str path;
    char *argv[5];
    int ok;
    struct stat st;

    ok = osr_pkg_install_step("Installing Steam", pkgs);

    str_init(&path);
    str_addz(&path, osr_mod_home());
    str_addz(&path, "/.bashrc");
    ok = osr_ensure_line(str_text(&path), "export STEAM_FORCE_DESKTOPUI_SCALING=1") && ok;
    str_reset(&path);
    str_addz(&path, osr_mod_home());
    str_addz(&path, "/.local/share/Steam");
    ok = osr_mkdir_p(str_text(&path)) && ok;
    str_free(&path);

    /* Steam resolves names through its own bundled libc, which cannot follow a
     * plain /etc/resolv.conf on a systemd-resolved box: point it at the stub. */
    if (dir_exists("/run/systemd/resolve") &&
        !(lstat("/etc/resolv.conf", &st) == 0 && S_ISLNK(st.st_mode))) {
        argv[0] = (char *)"ln"; argv[1] = (char *)"-sf";
        argv[2] = (char *)"../run/systemd/resolve/stub-resolv.conf";
        argv[3] = (char *)"/etc/resolv.conf"; argv[4] = NULL;
        (void)osr_run_root(argv);
    }
    return ok;
}
