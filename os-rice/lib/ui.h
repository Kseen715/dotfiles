/* lib/ui.h -- the live step window, for callers inside the core.
 *
 * `osr ui` (lib/ui.c) is driven from lib/ui.sh for shell modules; a module
 * written in C drives the same paint loop directly through these, which is
 * what lib/module.c's osr_run_step does. Not to be confused with lib/winui.h,
 * the Windows core's own console layer.
 *
 * C89 + POSIX.
 */
#ifndef OSR_POSIX_UI_H
#define OSR_POSIX_UI_H

#include <sys/types.h>

/* osr_ui_live -- does the live window apply? `[ -t 1 ] && [ -z "$OSR_VERBOSE" ]`
 * (§3 auto-degrade). */
int osr_ui_live(void);

/* osr_ui_spin_pid -- repaint the block until pid exits, tailing log_path.
 * Returns how many rows the last paint left on screen, which is what
 * osr_ui_result needs to erase.
 *
 * For a pid that is NOT our child: it polls with kill(pid, 0), which is what
 * lib/ui.sh's `_spin` did, and works there because the pid belongs to the
 * shell that reaps it. Waiting on our OWN child this way would hang forever --
 * an exited child stays a pid until someone reaps it -- so use
 * osr_ui_spin_child for that. */
int osr_ui_spin_pid(pid_t pid, const char *desc, const char *log_path);

/* osr_ui_spin_child -- the same window around a child of THIS process: reaps
 * it as part of the loop (waitpid(WNOHANG)) instead of asking whether the pid
 * still exists, and reports its exit status. */
int osr_ui_spin_child(pid_t pid, const char *desc, const char *log_path, int *exit_status);

/* osr_ui_result -- erase those rows and leave one `[ok]`/`[!!] <desc>` line. */
void osr_ui_result(int painted, int ok, const char *desc);

/* osr_ui_fail_tail -- the last n lines of a failed step's log, to stderr. */
void osr_ui_fail_tail(long n, const char *log_path);

/* osr_ui_append_log -- append the per-step log to $OSR_LOG, so the run log
 * stays complete (`cat "$_OSR_STEP_LOG" >>"$OSR_LOG"`). */
void osr_ui_append_log(const char *step_log);

#endif /* OSR_POSIX_UI_H */
