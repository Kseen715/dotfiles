/* lib/ui.h -- colored status lines, C port of lib/log.sh (+ ui.sh's step
 * counter). Same four tags, same meaning:
 *
 *   osr_info    [INFO]    stdout, cyan tag
 *   osr_warn    [WARN]    stderr, yellow tag, does not stop the run
 *   osr_error   [ERROR]   stderr, red tag, PRINTS THEN exit(1) -- same
 *                         "one fatal path" contract as lib/log.sh's error()
 *   osr_success [DONE]    stdout, green tag
 *
 * Not ported from lib/ui.sh: the live-repainting multi-line spinner window
 * (run_step/_step_paint). That's real complexity (needs an async child
 * process + a redraw loop) for a cosmetic feature; colored tags + a step
 * counter cover the substance of "looks like the sh CLI" without it. Noted
 * here rather than silently shipped as equivalent.
 *
 * C89. Windows: uses classic SetConsoleTextAttribute, not ANSI escape
 * codes -- real XP/Win7 consoles don't interpret VT sequences at all, only
 * the classic console API works there.
 */
#ifndef OSR_UI_H
#define OSR_UI_H

void osr_info(const char *fmt, ...);
void osr_warn(const char *fmt, ...);
void osr_success(const char *fmt, ...);

/* osr_error -- prints to stderr, then exit(1). Never returns. */
void osr_error(const char *fmt, ...);

/* osr_info_step -- osr_info with a "[03/12] " prefix, install.sh's
 * step_prefix()+info() combo. total == 0 omits the prefix entirely.
 */
void osr_info_step(unsigned long n, unsigned long total, const char *fmt, ...);

#endif /* OSR_UI_H */
