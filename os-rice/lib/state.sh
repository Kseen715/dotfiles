# lib/state.sh — what is currently applied (POSIX sh)
#
# ~/.config/osr/state, `key=value`, one per line. Small on purpose: this is not
# a database, it is the four answers something outside the installer needs.
#
#   rice=i3-rosemary     which manifest was installed (picks the module set a
#                        theme apply runs - see osr_theme_modules)
#   theme=nord           which theme is painted right now
#   wallpaper=/abs/path  the image last set
#   applied=1754...      unix time of the last theme apply
#
# It is user-owned data, not config: os-rice writes it, nothing reads it to
# decide what to install. A missing or corrupt state file must therefore never
# be fatal - every reader degrades to "unknown" and the system still applies.

# osr_state_file — path to the state file (honours OSR_HOME, so tests are hermetic).
osr_state_file() {
    printf '%s' "${OSR_HOME:-$HOME}/.config/osr/state"
}

# osr_state_get <key> — echo the value, "" when unset or the file is missing.
# Last assignment wins, matching how osr_state_set appends-then-rewrites.
osr_state_get() {
    _sg_f=$(osr_state_file)
    [ -f "$_sg_f" ] || return 0
    sed -n "s/^$1=//p" "$_sg_f" | tail -n 1
}

# osr_state_set <key> <value> — write one key, preserving the others.
#
# The whole file is rebuilt in memory and piped through `as_user tee`, the same
# idiom apply_wallpaper uses: writing a temp file as root and renaming it would
# leave a root-owned state file in the user's config dir (user-for-user, §8).
osr_state_set() {
    _ss_f=$(osr_state_file)
    _ss_body=""
    if [ -f "$_ss_f" ]; then
        _ss_body=$(grep -v "^$1=" "$_ss_f" || true)
    fi
    as_user mkdir -p "$(dirname "$_ss_f")"
    {
        [ -n "$_ss_body" ] && printf '%s\n' "$_ss_body"
        printf '%s=%s\n' "$1" "$2"
    } | as_user tee "$_ss_f" >/dev/null
}
