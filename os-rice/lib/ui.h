/* lib/ui.h -- the live step window, for callers inside the core.
 *
 * One header, two implementations (lib/ui.c holds both): the POSIX one paints
 * with ANSI escapes around a child it watches with kill/waitpid, the Windows
 * one with the console API around a CreateProcess handle. What differs is not
 * only the drawing -- it is the shape of the call, which is why the two halves
 * of this header are guarded rather than merged into one signature that would
 * fit neither.
 *
 * POSIX side: `osr ui` (lib/ui.c) is driven from lib/ui.sh for shell modules,
 * and a module written in C drives the same paint loop directly through the
 * spin/result pair, which is what lib/module.c's osr_run_step does.
 *
 * Windows side: nothing is driven from a shell, so there is one entry point,
 * and it takes the whole command LINE its callers hold rather than an argv
 * vector.
 *
 * C89 + POSIX, and C89 + Win32.
 */
#ifndef OSR_UI_H
#define OSR_UI_H

#ifndef _WIN32

#include <sys/types.h>

/* osr_ui_live -- does the live window apply? `[ -t 1 ] && [ -z "$OSR_VERBOSE" ]`
 * (section 3 auto-degrade). */
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

#else /* _WIN32 */

/* osr_run_step_cmd -- run cmd (a whole command line, handed to the platform's
 * command interpreter the way system() would) under a live status line
 * labeled desc. Returns cmd's exit code, 0 on success.
 *
 * Takes a command LINE rather than an argv vector because that is what its
 * callers hold: a `winget install --id ... -e` line, a vendor's `irm | iex`
 * one-liner. lib/module.h's osr_run_step is the argv-taking form a module
 * uses, and it composes a line for this.
 *
 * UNLIKE the sh run_step, this does not end the run on a non-zero exit: it
 * returns the code and lets the caller decide. Its callers -- the package
 * dispatch and the builders -- report and carry on; the fatal shape lives one
 * level up in osr_run_step, and the difference between the two is exactly
 * that function.
 */
int osr_run_step_cmd(const char *desc, const char *cmd);

#endif /* _WIN32 */

#endif /* OSR_UI_H */
