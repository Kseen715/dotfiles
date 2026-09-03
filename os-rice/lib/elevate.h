/* lib/elevate.h -- one-time privilege elevation.
 *
 * Windows port of install.sh's sudo warm-up + lib/user.sh's as_root split.
 * On Linux, install.sh runs `sudo -v` once at the top of a run (and keeps
 * the credential alive in the background) so that the handful of steps that
 * genuinely need root -- native package installs -- never prompt again
 * mid-run, while everything else keeps running as the user.
 *
 * Windows has no credential cache to warm, so the equivalent is to elevate
 * the *process* once: osr_elevate_now re-launches this same executable with
 * the same arguments under the `runas` verb, waits for it, and exits with
 * its status. One UAC prompt covers the whole remaining run, and every
 * later osr_is_admin() check inside the elevated child is simply true --
 * the same net effect as a warmed sudo credential.
 *
 * Two consequences worth knowing:
 *
 *   - Elevation cannot inherit a console, so the elevated child gets its
 *     own window. That is a property of UAC, not a choice made here.
 *   - The child is told the invoking user's profile via --user-home, which
 *     is this port's $SUDO_USER: an elevated run must still write configs
 *     into the profile being riced, not into whichever admin account
 *     answered the prompt. That mirrors lib/user.sh's as_user exactly.
 *
 * Callers must be prepared for a 0 return (prompt declined): elevation is
 * an optimization for the manager route, never the only way to install --
 * see lib/build.h's Windows toolkit for the no-admin fallback: everything a
 * builder installs goes under %LOCALAPPDATA%\\osr, which needs none.
 *
 * C89.
 */
#ifndef OSR_ELEVATE_H
#define OSR_ELEVATE_H

/* osr_elevate_init -- remember main()'s argv so a later osr_elevate_now can
 * relaunch this run verbatim. Call once, from main, before any work.
 */
void osr_elevate_init(int argc, char **argv);

/* osr_is_admin -- 1 when this process holds Administrator rights. */
int osr_is_admin(void);

/* osr_set_user_home -- point this process's home directory at `home`, so
 * every ~-relative config path resolves to the profile being riced rather
 * than to whoever answered the UAC prompt. This is what --user-home feeds,
 * and it is the whole reason that flag exists: lib/user.sh's as_user does
 * the same job on Linux by dropping back to $SUDO_USER.
 */
void osr_set_user_home(const char *home);

/* osr_elevate_now -- ensure the run is elevated, prompting at most once per
 * process. Returns 1 if already elevated. Otherwise it relaunches elevated
 * and, on success, never returns (this process exits with the child's exit
 * code). Returns 0 when the user declined the prompt or elevation failed,
 * in which case the caller should fall back to a route that needs no admin.
 * `reason` is shown to the user before the prompt.
 */
int osr_elevate_now(const char *reason);

#endif /* OSR_ELEVATE_H */
