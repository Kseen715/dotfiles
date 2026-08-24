/* lib/winui.h -- colored status lines, C port of lib/log.sh + lib/ui.sh (the
 * spinner/step window). Same four tags, same meaning:
 *
 *   osr_info    [INFO]    stdout, cyan tag
 *   osr_warn    [WARN]    stderr, yellow tag, does not stop the run
 *   osr_error   [ERROR]   stderr, red tag, PRINTS THEN exit(1) -- same
 *                         "one fatal path" contract as lib/log.sh's error()
 *   osr_success [DONE]    stdout, green tag
 *
 * osr_run_step is the C port of ui.sh's run_step/_step_paint/_spin: a
 * live-repainting block (dimmed tail of the command's own output, a
 * spinner line last) that collapses to one `[ok]`/`[!!] desc` line when
 * the command finishes, same TTY-only auto-degrade (§3) as the sh
 * original -- piped/redirected output or OSR_VERBOSE set gets plain
 * streamed lines instead. UNLIKE ui.sh's run_step, this does not call
 * osr_error() on a non-zero exit -- it returns the exit code and lets the
 * caller decide, because its actual caller (lib/winpkg.c's package-manager
 * dispatch) already has its own non-fatal "try the next manager" contract
 * ported from windows-rice/src/pkg.ps1's Install-RicePackage, which was
 * never a "one fatal path" design to begin with; forcing run_step's sh
 * fatality here would silently change that behavior.
 *
 * C89. Windows: uses classic console API (SetConsoleTextAttribute,
 * SetConsoleCursorPosition, FillConsoleOutputCharacterA), not ANSI escape
 * codes -- real XP/Win7 consoles don't interpret VT sequences at all, only
 * the classic console API works there.
 */
#ifndef OSR_WINUI_H
#define OSR_WINUI_H

/* OSR_TAG_WIDTH -- columns reserved for the bracketed tag, so every line
 * this installer prints starts its message in the same column: the log
 * tags ([INFO], [WARN], ...), run_step's [ok]/[!!] and the spinner frame
 * all pad out to it. Duplicated from lib/common.h on purpose -- the
 * Windows core shares no header with the POSIX one -- so the two must be
 * changed together. */
#ifndef OSR_TAG_WIDTH
#define OSR_TAG_WIDTH 8
#endif

void osr_info(const char *fmt, ...);
void osr_warn(const char *fmt, ...);
void osr_success(const char *fmt, ...);

/* osr_error -- prints to stderr, then exit(1). Never returns. */
void osr_error(const char *fmt, ...);

/* osr_info_step -- osr_info with a "[03/12] " prefix, install.sh's
 * step_prefix()+info() combo. total == 0 omits the prefix entirely.
 */
void osr_info_step(unsigned long n, unsigned long total, const char *fmt, ...);

/* osr_run_step -- run cmd (a full command line, passed to the platform's
 * command interpreter the same way system() would) under a live status
 * line labeled desc. Returns cmd's exit code (0 on success). See this
 * file's header comment for the sh-vs-C fatality difference.
 */
int osr_run_step(const char *desc, const char *cmd);

#endif /* OSR_WINUI_H */
