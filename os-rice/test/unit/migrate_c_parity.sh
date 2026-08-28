#!/bin/sh
# Proves lib/migrate.c patches a seeded, user-owned layer exactly as
# lib/migrate.sh did: the same skip conditions, the same appended bytes, the
# same byte-exact region match (and the same refusal when the user has edited
# it), the same once-only .pre-migrate backup and the same exit status.
#
# The one deliberate API difference is checked here rather than papered over:
# the shell version took the NAMES of two functions whose output was the old
# and the new region, the C version takes that text directly. The test drives
# each tier in its own idiom and compares what lands on disk.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip migrate_c_parity: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
BIN="$TMP/bin"; mkdir -p "$BIN"
REAL_SH=$(command -v sh)

for _t in env cat cut grep sed awk tr head tail printf id mktemp rm cp mv mkdir \
          ln ls find sort wc dirname basename tee test true false; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in
        /*) ln -sf "$_p" "$BIN/$_t" ;;
        *)  for _d in /usr/bin /bin /usr/local/bin; do
                [ -x "$_d/$_t" ] && { ln -sf "$_d/$_t" "$BIN/$_t"; break; }
            done ;;
    esac
done
ln -sf "$REAL_SH" "$BIN/sh"

FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB OSR_DISTRO=ubuntu OSR_ARCH=x86_64
       NO_COLOR=1 TERM=dumb OSR_PKG=apt OSR_INIT=systemd"
ME=$(id -un)

# seed <root> -- redefined per scenario; writes the file under migration.
seed() { :; }

run_side() {
    _root=$1; _tier=$2; _cmd=$3
    rm -rf "$_root"; mkdir -p "$_root"
    ROOT=$_root; seed
    if [ "$_tier" = sh ]; then
        # shellcheck disable=SC2086
        env -i PATH="$BIN" $FACTS TMPDIR="$_root" \
            OSR_USER="$ME" OSR_HOME="$_root" HOME="$_root" ROOT="$_root" \
            sh -c '
                . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"
                . "$OSR_LIB/migrate.sh"
                eval "$1"' _ "$_cmd" 2>&1 || :
    else
        # shellcheck disable=SC2086
        env -i PATH="$BIN" $FACTS TMPDIR="$_root" \
            OSR_USER="$ME" OSR_HOME="$_root" HOME="$_root" ROOT="$_root" \
            OSR_BIN="$OSR_BIN" \
            sh -c 'eval "$1"' _ "$_cmd" 2>&1 || :
    fi
}

# dump <root> -- every file the scenario left behind, with the sandbox path
# collapsed so two roots compare equal. Content included: a migration that
# writes the right log and the wrong bytes is still a broken migration.
dump() {
    (cd "$1" && find . -type f ! -path './bin/*' | sort | while read -r _f; do
        printf '=== %s\n' "$_f"; cat "$_f"
    done) | sed "s|$1|ROOT|g"
}

# scene <label> <sh-snippet> <c-snippet> -- both tiers, output and disk.
scene() {
    _label=$1
    _sh_out=$(run_side "$TMP/a" sh "$2" | sed "s|$TMP/a|ROOT|g")
    _c_out=$(run_side "$TMP/b" c "$3" | sed "s|$TMP/b|ROOT|g")
    assert_eq "$_sh_out" "$_c_out" "$_label: same output"
    assert_eq "$(dump "$TMP/a")" "$(dump "$TMP/b")" "$_label: same files"
}

# status <label> <sh-snippet> <c-snippet> -- the exit status, which is what the
# module scripts branch their `|| migrate_stale` fallback on.
status() {
    _label=$1
    run_side "$TMP/a" sh "$2" >/dev/null 2>&1; _a=$?
    run_side "$TMP/b" c "$3" >/dev/null 2>&1; _b=$?
    assert_eq "$_a" "$_b" "$_label: same status"
}

RC="\$ROOT/00-env.zsh"

# --- 1. migrate_append --------------------------------------------------------
seed() { printf 'export EDITOR=vi\n' >"$ROOT/00-env.zsh"; }
scene "an absent pattern is appended" \
    "migrate_append $RC 'typeset -U path' 'typeset -U path PATH' <<'EOF'
typeset -U path PATH
EOF" \
    "printf 'typeset -U path PATH\n' | \"\$OSR_BIN\" migrate append $RC 'typeset -U path' 'typeset -U path PATH'"

seed() { printf 'export EDITOR=vi\ntypeset -U path PATH\n' >"$ROOT/00-env.zsh"; }
scene "an already migrated file is untouched" \
    "migrate_append $RC 'typeset -U path' 'typeset -U path PATH' <<'EOF'
typeset -U path PATH
EOF" \
    "printf 'typeset -U path PATH\n' | \"\$OSR_BIN\" migrate append $RC 'typeset -U path' 'typeset -U path PATH'"

seed() { :; }
scene "a file that does not exist is skipped" \
    "migrate_append $RC 'typeset -U path' 'typeset -U path PATH' <<'EOF'
typeset -U path PATH
EOF" \
    "printf 'typeset -U path PATH\n' | \"\$OSR_BIN\" migrate append $RC 'typeset -U path' 'typeset -U path PATH'"

# A file whose last line has no newline: the append must not weld itself onto it.
seed() { printf 'export EDITOR=vi' >"$ROOT/00-env.zsh"; }
scene "a file with no trailing newline" \
    "migrate_append $RC 'typeset -U path' 'typeset -U path PATH' <<'EOF'
typeset -U path PATH
EOF" \
    "printf 'typeset -U path PATH\n' | \"\$OSR_BIN\" migrate append $RC 'typeset -U path' 'typeset -U path PATH'"

# The backup is taken from the pre-migration state and never refreshed.
seed() { printf 'one\n' >"$ROOT/00-env.zsh"; printf 'ORIGINAL\n' >"$ROOT/00-env.zsh.pre-migrate"; }
scene "an existing backup is not overwritten" \
    "migrate_append $RC 'two' 'add two' <<'EOF'
two
EOF" \
    "printf 'two\n' | \"\$OSR_BIN\" migrate append $RC 'two' 'add two'"

# --- 2. migrate_replace -------------------------------------------------------
OLD='if command -v brew >/dev/null 2>&1; then
  eval "$(brew shellenv)"
fi'
NEW='if [ -x /opt/homebrew/bin/brew ]; then
  eval "$(/opt/homebrew/bin/brew shellenv)"
fi'

seed() { printf 'export EDITOR=vi\n%s\nexport PAGER=less\n' "$OLD" >"$ROOT/00-env.zsh"; }
scene "an untouched region is replaced" \
    "_old() { cat <<'EOF'
$OLD
EOF
}
_new() { cat <<'EOF'
$NEW
EOF
}
migrate_replace $RC 'brew probe -> absolute path' _old _new" \
    "\"\$OSR_BIN\" migrate replace $RC 'brew probe -> absolute path' '$OLD' '$NEW'"

status "a replaced region reports success" \
    "_old() { cat <<'EOF'
$OLD
EOF
}
_new() { cat <<'EOF'
$NEW
EOF
}
migrate_replace $RC 'brew probe -> absolute path' _old _new" \
    "\"\$OSR_BIN\" migrate replace $RC 'brew probe -> absolute path' '$OLD' '$NEW'"

# One edited character and the region is no longer ours: the caller must be
# told so it can warn instead of rewriting a hand-tuned file.
seed() { printf 'export EDITOR=vi\n%s\n' "$(printf '%s' "$OLD" | sed 's/brew shellenv/brew shellenv --no-op/')" >"$ROOT/00-env.zsh"; }
scene "an edited region is refused" \
    "_old() { cat <<'EOF'
$OLD
EOF
}
_new() { cat <<'EOF'
$NEW
EOF
}
migrate_replace $RC 'brew probe -> absolute path' _old _new" \
    "\"\$OSR_BIN\" migrate replace $RC 'brew probe -> absolute path' '$OLD' '$NEW'"

status "an edited region reports failure" \
    "_old() { cat <<'EOF'
$OLD
EOF
}
_new() { cat <<'EOF'
$NEW
EOF
}
migrate_replace $RC 'brew probe -> absolute path' _old _new" \
    "\"\$OSR_BIN\" migrate replace $RC 'brew probe -> absolute path' '$OLD' '$NEW'"

# Deletion: an empty replacement removes the region outright.
seed() { printf 'keep\n%s\nkeep2\n' "$OLD" >"$ROOT/00-env.zsh"; }
scene "an empty replacement deletes the region" \
    "_old() { cat <<'EOF'
$OLD
EOF
}
_new() { :; }
migrate_replace $RC 'drop the brew probe' _old _new" \
    "\"\$OSR_BIN\" migrate replace $RC 'drop the brew probe' '$OLD' ''"

# The needle is matched literally, which is the whole safety property: these
# regions are full of regex metacharacters.
seed() { printf 'a\n[ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh"  # *loads* nvm\nb\n' >"$ROOT/00-env.zsh"; }
scene "metacharacters are matched literally" \
    "_old() { cat <<'EOF'
[ -s \"\$NVM_DIR/nvm.sh\" ] && . \"\$NVM_DIR/nvm.sh\"  # *loads* nvm
EOF
}
_new() { printf 'source \"\$OSR_RCDIR/30-tools.zsh\"\n'; }
migrate_replace $RC 'legacy nvm -> 30-tools.zsh' _old _new" \
    "\"\$OSR_BIN\" migrate replace $RC 'legacy nvm -> 30-tools.zsh' '[ -s \"\$NVM_DIR/nvm.sh\" ] && . \"\$NVM_DIR/nvm.sh\"  # *loads* nvm' 'source \"\$OSR_RCDIR/30-tools.zsh\"'"

seed() { :; }
status "a missing file reports failure" \
    "_old() { printf 'x\n'; }
_new() { printf 'y\n'; }
migrate_replace $RC 'nothing to do' _old _new" \
    "\"\$OSR_BIN\" migrate replace $RC 'nothing to do' 'x' 'y'"

# An empty needle would "match" at offset 0 and splice the replacement in at
# the top of the file.
seed() { printf 'untouched\n' >"$ROOT/00-env.zsh"; }
scene "an empty region matches nothing" \
    "_old() { :; }
_new() { printf 'INJECTED\n'; }
migrate_replace $RC 'empty needle' _old _new" \
    "\"\$OSR_BIN\" migrate replace $RC 'empty needle' '' 'INJECTED'"

# --- 3. migrate_stale ---------------------------------------------------------
seed() { printf 'if command -v brew >/dev/null; then :; fi\n' >"$ROOT/00-env.zsh"; }
scene "a code line still holding the legacy warns" \
    "migrate_stale $RC 'command -v brew' 'a PATH-dependent brew probe'" \
    "\"\$OSR_BIN\" migrate stale $RC 'command -v brew' 'a PATH-dependent brew probe'"

# The replacement text explains what it replaced, so a naive grep matches the
# fix itself. Skipping comments is what makes a successful migration quiet.
seed() { printf '# never uses command -v brew any more\nexport EDITOR=vi\n' >"$ROOT/00-env.zsh"; }
scene "a comment mentioning it stays quiet" \
    "migrate_stale $RC 'command -v brew' 'a PATH-dependent brew probe'" \
    "\"\$OSR_BIN\" migrate stale $RC 'command -v brew' 'a PATH-dependent brew probe'"

seed() { printf '   # indented comment: command -v brew\n' >"$ROOT/00-env.zsh"; }
scene "an indented comment stays quiet too" \
    "migrate_stale $RC 'command -v brew' 'a PATH-dependent brew probe'" \
    "\"\$OSR_BIN\" migrate stale $RC 'command -v brew' 'a PATH-dependent brew probe'"

seed() { printf '\tif command -v brew; then :; fi\n' >"$ROOT/00-env.zsh"; }
scene "an indented code line still warns" \
    "migrate_stale $RC 'command -v brew' 'a PATH-dependent brew probe'" \
    "\"\$OSR_BIN\" migrate stale $RC 'command -v brew' 'a PATH-dependent brew probe'"

seed() { :; }
scene "a missing file is not stale" \
    "migrate_stale $RC 'command -v brew' 'a PATH-dependent brew probe'" \
    "\"\$OSR_BIN\" migrate stale $RC 'command -v brew' 'a PATH-dependent brew probe'"

seed() { printf 'export EDITOR=vi\n' >"$ROOT/00-env.zsh"; }
scene "a clean file is not stale" \
    "migrate_stale $RC 'command -v brew' 'a PATH-dependent brew probe'" \
    "\"\$OSR_BIN\" migrate stale $RC 'command -v brew' 'a PATH-dependent brew probe'"

finish
