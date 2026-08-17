# migrate.sh — patch the layers install_layer can never reach.
#
# 00-env.zsh and 99-local.zsh are seeded once and are then user territory (§5).
# That is exactly what keeps a rice switch non-destructive — but it also means a
# fix shipped in the repo never reaches a box installed before it: install_layer
# owns only the 10-/20-/30- layers, and seed_once deliberately skips a file that
# already exists. Every perf fix that landed in 00-env.zsh would have applied to
# new machines only.
#
# These helpers close that gap without breaking the ownership rule. They only
# ever do one of two things:
#
#   - append text that cannot destroy anything, or
#   - replace a region that matches, byte for byte, what os-rice itself shipped.
#
# A region the user actually edited fails the exact match, and the caller reports
# it instead of guessing, so a hand-tuned layer is never silently rewritten. That
# asymmetry is the whole design: automatic when it is provably our own text,
# advisory the moment it is not.
#
# All of it is content-addressed rather than gated on ~/.config/osr/state: that
# file is documented as data nothing reads to decide what to install, and a
# migration keyed on it would silently skip a box whose state was lost.

# _migrate_backup <file> — one .pre-migrate copy per file, first touch only.
# Mirrors backup_copy's once-only .bak: a second migration in the same run must
# not overwrite the copy that still shows the pre-migration state.
_migrate_backup() {
    [ -f "$1.pre-migrate" ] && return 0
    as_user cp "$1" "$1.pre-migrate"
}

# migrate_append <file> <detect_ere> <label>  <<'EOF' … EOF
#   Append the heredoc to <file> when <detect_ere> matches nothing in it.
#   Purely additive, so it is safe on a user-owned file whatever they put there.
migrate_append() {
    _ma_file=$1
    _ma_pat=$2
    _ma_label=$3
    # stdin must be drained even when skipping, or the caller's heredoc leaks
    # into the next command in the script.
    if [ ! -f "$_ma_file" ] || grep -qE "$_ma_pat" "$_ma_file" 2>/dev/null; then
        cat >/dev/null
        return 0
    fi
    _migrate_backup "$_ma_file"
    { printf '\n'; cat; } | as_user tee -a "$_ma_file" >/dev/null
    info "migrated ${_ma_file##*/}: $_ma_label"
}

# migrate_replace <file> <label> <old_fn> <new_fn>
#   Replace the first exact occurrence of <old_fn>'s output with <new_fn>'s.
#   <new_fn> may print nothing, which deletes the region. Returns non-zero when
#   the region is absent or no longer byte-identical — that is the signal for the
#   caller to warn rather than force anything.
#
#   Idempotent by construction: after a successful run the old text is gone, so a
#   re-run simply finds no match and returns non-zero without touching the file.
migrate_replace() {
    _mr_file=$1
    _mr_label=$2
    _mr_old=$3
    _mr_new=$4
    [ -f "$_mr_file" ] || return 1

    _mr_o="${TMPDIR:-/tmp}/osr-mig-old-$$"
    _mr_n="${TMPDIR:-/tmp}/osr-mig-new-$$"
    _mr_r="${TMPDIR:-/tmp}/osr-mig-res-$$"
    "$_mr_old" >"$_mr_o"
    "$_mr_new" >"$_mr_n"

    # Whole-file string replace. awk rather than sed because the regions span
    # lines and contain regex metacharacters ($, [, *, \) that would have to be
    # escaped to survive sed — index() takes them literally, which is also what
    # makes the match exact.
    if awk -v OLDF="$_mr_o" -v NEWF="$_mr_n" '
        BEGIN {
            while ((getline l < OLDF) > 0) old = old l "\n"
            while ((getline l < NEWF) > 0) new = new l "\n"
            if (old == "") exit 1          # empty needle would "match" at 1
        }
        { body = body $0 "\n" }
        END {
            i = index(body, old)
            if (i == 0) exit 1
            printf "%s%s%s", substr(body, 1, i - 1), new, substr(body, i + length(old))
        }
    ' "$_mr_file" >"$_mr_r"; then
        _migrate_backup "$_mr_file"
        as_user cp "$_mr_r" "$_mr_file"
        info "migrated ${_mr_file##*/}: $_mr_label"
        rm -f "$_mr_o" "$_mr_n" "$_mr_r"
        return 0
    fi

    rm -f "$_mr_o" "$_mr_n" "$_mr_r"
    return 1
}

# migrate_stale <file> <detect_ere> <what> — warn, once, that a legacy pattern is
# still present but no longer matches anything os-rice shipped, so it must be
# resolved by hand. Never fatal: a box that cannot be auto-patched still installs.
#
# <detect_ere> is matched against CODE lines only — a line whose first non-blank
# character is not `#`. The replacement text these migrations write explains what
# it replaced ("never `command -v brew`", "an eager nvm.sh source"), so a naive
# grep matches the fix itself and warns on every run forever after. Skipping
# comments is what makes a successful migration actually quiet on the next run.
migrate_stale() {
    [ -f "$1" ] || return 0
    grep -qE "^[[:space:]]*[^#[:space:]].*($2)" "$1" 2>/dev/null || return 0
    warn "$1 still has $3, edited so it cannot be patched automatically - see zsh/rc.d/ in the dotfiles repo for the current version"
}
