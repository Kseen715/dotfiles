/* modules/waydroid.c -- Waydroid (Android in a container) + GApps image + the
 * ARM translation layer for the detected CPU. POSIX port of .../modules/waydroid.sh.
 * Needs a real kernel (binder), systemd, and network -> validated on hardware,
 * not CI (§9). Available module (not in the default rice.list).
 *
 * Port of modules/waydroid.sh, kept as the reference at
 * test/ref/waydroid_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/git.h"

#include <stddef.h>
#include <string.h>

int osrm_waydroid(void) {
    static const char *const pkgs[] = { "waydroid", "waydroid-image-gapps", NULL };
    Str src, venv, pip, py, req, main;
    char *argv[8];
    const char *vendor = env_str("OSR_CPU_VENDOR", "");
    int ok;

    ok = osr_pkg_install_step("Installing Waydroid (AUR)", pkgs);
    argv[0] = (char *)"waydroid"; argv[1] = (char *)"init"; argv[2] = (char *)"-s";
    argv[3] = (char *)"GAPPS"; argv[4] = NULL;
    ok = osr_run_step_root("Initializing Waydroid (GApps)", argv) && ok;

    /* waydroid_script is what installs the ARM translation layer: upstream
     * ships no package, and the libs it fetches are per-vendor. */
    str_init(&src);
    str_addz(&src, env_str("TMPDIR", "/tmp"));
    str_addz(&src, "/waydroid_script");
    {
        char *shallow[3];
        shallow[0] = (char *)"--depth"; shallow[1] = (char *)"1"; shallow[2] = NULL;
        ok = osr_git_repo("waydroid_script",
                          "https://github.com/casualsnek/waydroid_script.git",
                          str_text(&src), shallow) && ok;
    }

    str_init(&venv); str_init(&pip); str_init(&py); str_init(&req); str_init(&main);
    str_addz(&venv, str_text(&src)); str_addz(&venv, "/venv");
    str_addz(&pip,  str_text(&venv)); str_addz(&pip, "/bin/pip");
    str_addz(&py,   str_text(&venv)); str_addz(&py,  "/bin/python");
    str_addz(&req,  str_text(&src));  str_addz(&req, "/requirements.txt");
    str_addz(&main, str_text(&src));  str_addz(&main, "/main.py");

    /* --clear: a venv left over from a failed run has whatever half-resolved
     * dependency set killed it. */
    argv[0] = (char *)"python3"; argv[1] = (char *)"-m"; argv[2] = (char *)"venv";
    argv[3] = (char *)"--clear"; argv[4] = venv.p; argv[5] = NULL;
    ok = osr_run_step_user("Setting up waydroid_script venv", argv) && ok;
    argv[0] = pip.p; argv[1] = (char *)"install"; argv[2] = (char *)"-r";
    argv[3] = req.p; argv[4] = NULL;
    ok = osr_run_step_user("Installing waydroid_script deps", argv) && ok;

    argv[0] = py.p; argv[1] = main.p; argv[2] = (char *)"install";
    argv[4] = NULL;
    if (strcmp(vendor, "GenuineIntel") == 0) {
        argv[3] = (char *)"libhoudini";
        ok = osr_run_step_user("Installing libhoudini (Intel)", argv) && ok;
    } else if (strcmp(vendor, "AuthenticAMD") == 0) {
        argv[3] = (char *)"libndk";
        ok = osr_run_step_user("Installing libndk (AMD)", argv) && ok;
    } else {
        osr_warnf("unsupported CPU vendor '%s' for Waydroid ARM libs - skipping", vendor);
    }
    ok = osr_service_enable("waydroid-container") && ok;

    str_free(&src); str_free(&venv); str_free(&pip);
    str_free(&py); str_free(&req); str_free(&main);
    return ok;
}
