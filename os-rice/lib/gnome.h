/* lib/gnome.h -- GNOME session detection and custom-keybinding plumbing, the C
 * port of lib/gnome.sh. One copy for every module that wants a Win-key
 * shortcut in a GNOME session (wofi's Super+R, cliphist's Super+V, whatever
 * comes next).
 *
 * A GNOME session has no compositor config file to write a keybind into the
 * way a Hyprland or i3 rice does; the shortcut lives in dconf, under
 * org.gnome.settings-daemon.plugins.media-keys, and is registered through
 * gsettings. Two steps, always in this order:
 *
 *   1. osr_gnome_free_binding("<Super>r")
 *   2. osr_gnome_keybind("wofi", "Application Launcher", "<Super>r", "sh -c '...'")
 *
 * Everything here is idempotent and safe to re-run.
 *
 * C89 + POSIX.
 */
#ifndef OSR_GNOME_H
#define OSR_GNOME_H

/* osr_gnome_is_session -- 1 when this is a GNOME session. Two variables are
 * read because Ubuntu sets XDG_CURRENT_DESKTOP=ubuntu:GNOME while upstream
 * GNOME sets plain GNOME, and a display manager may only set
 * XDG_SESSION_DESKTOP. */
int osr_gnome_is_session(void);

/* osr_gnome_free_binding -- unbind every GNOME Shell / mutter key holding this
 * chord, so a custom shortcut can take it: an upstream keybinding on the same
 * chord silently WINS over a custom one, leaving a dead shortcut with nothing
 * to see. The key list is derived from what gsettings reports rather than
 * hand-kept, so a key that gains the binding in a future GNOME is freed the
 * day it does. The match is the binding quoted on both sides, so freeing
 * "<Super>r" leaves "<Shift><Super>r" -- a different chord in the same list --
 * alone. */
int osr_gnome_free_binding(const char *binding);

/* osr_gnome_keybind -- register a custom shortcut at
 * .../custom-keybindings/<id>/. Idempotent: a path already in the parent list
 * is left as it is.
 *
 * gnome-settings-daemon splits command with shell-style ARGV parsing but never
 * runs a shell of its own, so anything with a pipe or an `||` in it has to be
 * spelled as an explicit `sh -c '...'` by the caller. */
int osr_gnome_keybind(const char *id, const char *name, const char *binding,
                      const char *command);

#endif /* OSR_GNOME_H */
