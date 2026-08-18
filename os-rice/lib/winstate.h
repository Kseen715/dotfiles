/* lib/winstate.h -- what is currently applied, C port of lib/state.sh.
 * `%USERPROFILE%\.config\osr\state`, `key=value` lines -- same path shape
 * windows-rice already uses for app configs (`~\.config\fastfetch\...`),
 * so this state file lives in the same tree a user would expect to find
 * `.config\osr\` in on this OS.
 *
 * C89.
 */
#ifndef OSR_WINSTATE_H
#define OSR_WINSTATE_H

/* osr_state_get -- copy the value for key into out (bounded); out is "" if
 * the key is unset or the state file doesn't exist yet. Last assignment
 * wins, matching osr_state_set's rewrite-the-whole-file behavior.
 */
void osr_state_get(const char *key, char *out, unsigned long out_sz);

/* osr_state_set -- write key=value, preserving every other key. Creates
 * the state directory if needed. Returns 1 on success, 0 on I/O failure.
 */
int osr_state_set(const char *key, const char *value);

#endif /* OSR_WINSTATE_H */
