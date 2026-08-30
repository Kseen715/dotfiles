/* lib/reload.h -- make a swapped layer visible without a logout, the C port of
 * lib/reload.sh.
 *
 * Installing the files is half a theme switch; the running programs still hold
 * the old ones. Every reloader here is best-effort and independent:
 *
 *   - probe first, act second. A liveness check, because i3-msg on a Wayland
 *     box is not an error to report, it is a program that is not running.
 *   - never fatal. A reload that fails leaves the file on disk correct and the
 *     app repainting at its next start; aborting the switch would leave the
 *     desktop half-painted instead, which is strictly worse.
 *   - never restart what would lose state. dunst/mako are reloaded, not
 *     killed; a terminal is left alone entirely (its palette is re-read per
 *     window, and restarting it would close a shell someone is typing in).
 *
 * Deliberately NOT here: logind/DM restarts, `hyprctl dispatch exit`, killing
 * the compositor. A theme switch may never end a session.
 *
 * C89 + POSIX.
 */
#ifndef OSR_RELOAD_H
#define OSR_RELOAD_H

int osr_reload_x11(void);      /* X resources, the i3 stack, the X compositor */
int osr_reload_wayland(void);  /* the Hyprland stack */
int osr_reload_notify(void);   /* the notification daemons, either session */
int osr_reload_gtk(void);      /* GTK/Qt theme names for apps already running */

/* osr_reload_all -- every reloader that applies to this session, then one line
 * naming what was reloaded. Always returns 0. */
int osr_reload_all(void);

#endif /* OSR_RELOAD_H */
