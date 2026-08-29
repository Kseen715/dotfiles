#!/bin/sh
# Proves the `# themable:` marker — the thing that decides whether installing a
# module puts a theme picker in front of the user — matches what the modules
# actually do.
#
# The marker is declared rather than inferred, because install.sh has to know
# the answer BEFORE it sources anything, and because grepping a script for
# "$OSR_THEME" at run time makes the picker appear or vanish on a comment edit.
# The cost of declaring it is that it can drift, which is what this file is: the
# inference runs here, in the test suite, where being wrong is a red build
# instead of a module that silently installs unpainted.
#
# Hermetic: reads the tree, runs nothing.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"
. "$HERE/../lib.sh"

# Everything through which a module can reach the resolved theme: the variables
# themselves, the lib/config.sh helpers that read them on the module's behalf,
# and lib/theme.sh's accessors. Comments are stripped first — prose about the
# theme picker is not a theme layer, and several modules carry exactly that.
THEME_USE='OSR_THEME|install_theme_layer|apply_config|install_wallpaper_layer|osr_theme_|osr_install_wallpaper'

_bad=""
_n_yes=0
# The sh module tier is empty now that every module is C (DESIGN §11a), so
# this loop is vacuous — kept, and guarded against the unmatched glob, because
# the marker rule still applies the moment a .sh module is added back.
for _f in "$OSR_ROOT"/modules/*.sh; do
    [ -f "$_f" ] || continue
    _m=$(basename "$_f" .sh)
    if sed 's/#.*//' "$_f" | grep -qE "$THEME_USE"; then _uses=1; else _uses=0; fi
    if "$OSR_BIN" module themable "$_m"; then _marked=1; else _marked=0; fi

    # A C module of the same name wins the dispatch, so it also owns the answer
    # and the .sh file beside it is dead weight for this purpose.
    if "$OSR_BIN" module has "$_m"; then continue; fi

    if [ "$_uses" = 1 ] && [ "$_marked" = 0 ]; then
        _bad="$_bad $_m:missing-marker"
    elif [ "$_uses" = 0 ] && [ "$_marked" = 1 ]; then
        _bad="$_bad $_m:stale-marker"
    fi
    if [ "$_marked" = 1 ]; then _n_yes=$((_n_yes + 1)); fi
done
assert_eq "" "$_bad" "every module's '# themable:' marker matches its theme usage"

# The count is a canary on the reading itself: a bug that answered "no" for
# everything would leave the loop above with nothing to disagree about. It spans
# BOTH tiers, because a module moving to C moves its answer from the header to
# its registry row and the total must not fall as the port proceeds.
for _m in $("$OSR_BIN" module list); do
    if "$OSR_BIN" module themable "$_m"; then _n_yes=$((_n_yes + 1)); fi
done
if [ "$_n_yes" -ge 20 ]; then
    ok "$_n_yes modules across both tiers declare themselves themable"
else
    fail "only $_n_yes themable modules - the marker is probably not being read"
fi

# The marker lives in the header block, beside `# session:`, so it is findable
# without reading the script. A `# themable:` line further down is prose.
_late=""
for _f in "$OSR_ROOT"/modules/*.sh; do
    [ -f "$_f" ] || continue
    _hdr=$(awk '/^#/ {print; next} {exit}' "$_f" | grep -c '^# themable:' || :)
    _all=$(grep -c '^# themable:' "$_f" || :)
    [ "$_hdr" = "$_all" ] || _late="$_late $(basename "$_f")"
done
assert_eq "" "$_late" "every '# themable:' marker sits in the header block"

# The C tier answers the same question for its own modules, and at least one of
# them must be themable or the flag in lib/modules.c's row is untested.
"$OSR_BIN" module themable fastfetch || fail "fastfetch installs a theme layer but is not marked themable"
ok "the C tier reports fastfetch as themable"
"$OSR_BIN" module themable docker && fail "docker reads no theme but is marked themable" || :
ok "the C tier reports docker as not themable"

# A name that is neither kind is not themable rather than an error: install.sh
# validates module names before it ever asks.
"$OSR_BIN" module themable no-such-module && fail "an unknown module must not be themable" || :
ok "an unknown module reports not themable"

# --- the behaviour the marker exists for --------------------------------------
#
# A tree small enough to run the installer in module mode for real. The runner
# is C now (`osr install run`, which install.sh is a shim over), so the theme
# path it took is read where it announces itself rather than out of a stubbed
# shell function:
#
#   themable set        -> "theme: <name>"
#   non-themable set    -> neither line; nothing is asked and nothing resolved
#   nothing recorded    -> the picker, which with no TTY says so and takes the
#                          default theme
#   a theme recorded    -> that one, with no picker line
#
# PATH is reduced to a stub bin so the fixture modules are all that runs.
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP/lib" "$TMP/modules" "$TMP/rices" "$TMP/home" "$TMP/bin"
ln -sfn "$OSR_ROOT/build" "$TMP/build"
ln -sfn "$OSR_ROOT/themes" "$TMP/themes"
cp "$OSR_ROOT/install.sh" "$TMP/install.sh"
for _f in "$OSR_LIB"/*.sh; do ln -sfn "$_f" "$TMP/lib/$(basename "$_f")"; done
for _t in sh env cat grep sed awk printf id rm mkdir mktemp test true false tee \
          cp chmod touch cut tr head sort wc dirname basename find date uname od; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$TMP/bin/$_t" ;; esac
done
printf '#!/bin/sh\n[ "$1" = "-u" ] && shift 2\nexec "$@"\n' >"$TMP/bin/sudo"
chmod +x "$TMP/bin/sudo"

# The two fixture modules: one declares a theme layer, one does not. They are
# .sh modules, which also keeps the runner's coexistence path exercised.
printf '# session: x11\n# themable: yes\n:\n' >"$TMP/modules/painted.sh"
printf '# session: x11\n:\n' >"$TMP/modules/plain.sh"
printf 'tester:x:1000:1000::%s:/bin/sh\n' "$TMP/home" >"$TMP/passwd"

_run() {
    env -i PATH="$TMP/bin" NO_COLOR=1 TERM=dumb USER=tester \
        HOME="$TMP/home" OSR_PASSWD_FILE="$TMP/passwd" \
        sh "$TMP/install.sh" --module "$@" </dev/null 2>&1 || :
}

case "$(_run plain)" in
    *"theme: "*) fail "installing a non-themable module still resolved a theme" ;;
    *) ok "a module set that reads no theme resolves none" ;;
esac
case "$(_run painted)" in
    *"theme: "*) ok "a themable module still resolves a theme" ;;
    *) fail "installing a themable module skipped theme resolution" ;;
esac
case "$(_run plain painted)" in
    *"theme: "*) ok "one themable module in the set is enough to resolve" ;;
    *) fail "a mixed module set skipped theme resolution" ;;
esac
# With nothing recorded yet there is no answer to reuse, so the picker path is
# reached - and with no TTY it says so before taking the default.
case "$(_run painted)" in
    *"no interactive terminal"*) ok "no theme applied yet: resolution is left to the picker" ;;
    *) fail "a box with no recorded theme did not reach the picker: $(_run painted)" ;;
esac
# ...but once a theme IS applied, it is the answer the picker would ask for, so
# the module install takes it instead of stopping to ask again.
mkdir -p "$TMP/home/.config/osr"
printf 'rice=demo\ntheme=nord\n' >"$TMP/home/.config/osr/state"
case "$(_run painted)" in
    *"no interactive terminal"*) fail "the recorded theme was ignored: $(_run painted)" ;;
    *"theme: nord"*) ok "the theme already applied is used without asking" ;;
    *) fail "the recorded theme was ignored: $(_run painted)" ;;
esac
# A recorded theme that has since been removed from themes/ is not an answer any
# more: back to asking, rather than aborting the run over stale state.
printf 'rice=demo\ntheme=deleted-theme\n' >"$TMP/home/.config/osr/state"
case "$(_run painted)" in
    *"no interactive terminal"*) ok "a recorded theme that no longer exists falls back to asking" ;;
    *) fail "a stale recorded theme was used: $(_run painted)" ;;
esac
rm -f "$TMP/home/.config/osr/state"

# An explicit --theme is an instruction, not a question, so it is honoured even
# where nothing will read it.
case "$(_run --theme nord plain)" in
    *"theme: nord"*) ok "an explicit --theme is honoured for any module set" ;;
    *) fail "--theme was dropped for a non-themable module set" ;;
esac

finish
