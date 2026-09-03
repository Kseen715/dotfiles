/* modules/pwsh.c -- PowerShell 7 and the profile it reads.
 *
 * A Windows-only module, in the same shape every module has: one file in
 * modules/, one osrm_<name>(void), and a POSIX branch that is empty because
 * there is nothing there for it to do. The prefix-free name (not win-pwsh) is
 * deliberate -- pwsh IS an app module: it installs a program and paints its
 * config, exactly like fastfetch. The win- group is the one that does not.
 *
 * NO THEME LAYER. The profile is dotfiles-owned, not theme-owned: it wires up
 * the prompt engine and the shell's behaviour, neither of which a palette has
 * anything to say about. That is why a `--theme-only` run does nothing here
 * (the install verbs are neutralized, and there is no config verb left behind
 * them) rather than being special-cased anywhere.
 *
 * THE PROFILE PATH IS ASKED OF pwsh ITSELF, never assembled from
 * %USERPROFILE%\Documents. A redirected or OneDrive-moved Documents folder
 * makes those two disagree, and the failure is silent: the file lands
 * somewhere real, and the shell reads a different real file. Port of
 * pwsh.ps1's Resolve-PwshProfilePath, which asked for the same reason.
 *
 * C89.
 */
#include "../lib/module.h"

#include <stddef.h>

#ifdef _WIN32

int osrm_pwsh(void) {
    static const char *const pkgs[] = { "pwsh", NULL };
    char *argv[5];
    Str profile, src;
    int ok;

    ok = osr_pkg_install_step("Installing PowerShell 7", pkgs);

    /* $PROFILE.CurrentUserCurrentHost is pwsh's own answer to "which file do
     * you read", which is the only trustworthy one -- see the file header. */
    argv[0] = (char *)"pwsh";
    argv[1] = (char *)"-NoLogo";
    argv[2] = (char *)"-NoProfile";
    argv[3] = (char *)"-Command";
    argv[4] = NULL;
    str_init(&profile);
    {
        char *cmd[6];
        cmd[0] = argv[0]; cmd[1] = argv[1]; cmd[2] = argv[2]; cmd[3] = argv[3];
        cmd[4] = (char *)"$PROFILE.CurrentUserCurrentHost";
        cmd[5] = NULL;
        if (!osr_run_capture(cmd, &profile)) profile.len = 0;
    }
    str_trim_trailing(&profile, '\n');
    str_trim_trailing(&profile, '\r');

    if (profile.len == 0) {
        osr_warnf("pwsh: could not ask pwsh for its own profile path; is pwsh installed?");
        str_free(&profile);
        return 0;
    }

    str_init(&src);
    str_addz(&src, osr_mod_dotfiles());
    str_addz(&src, "/PowerShell7-profile/Microsoft.PowerShell_profile.ps1");
    ok = osr_install_layer(str_text(&src), str_text(&profile)) && ok;
    if (ok) osr_successf("pwsh: profile installed -> %s", str_text(&profile));

    str_free(&src);
    str_free(&profile);
    return ok;
}

#else /* !_WIN32 */

/* PowerShell 7 does run on Linux, and a rice could install it -- but nothing
 * in this tree's shell layers reads a pwsh profile, so installing one would
 * paint a shell nobody here uses. An empty branch, not a stub with a warning:
 * the module has no row in lib/modules.c on this side, so it is never reached. */
int osrm_pwsh(void) { return 0; }

#endif /* _WIN32 */
