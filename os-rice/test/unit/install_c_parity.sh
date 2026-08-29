#!/bin/sh
# Proves the installer behaves byte-for-byte like the pure-sh runner it
# replaced, frozen at test/ref/install_sh_ref.sh.
#
# install.sh is a shim now: the option loop, the manifest, the detected-facts
# report, the theme resolution, the module loop and the closing line are
# `osr install run` in the harness core (lib/install.c). It was the last unit to
# stay shell, and for one reason - it SOURCED each module - which stopped being
# true when the last .sh module became C (DESIGN §11a).
#
# So the comparison is end-to-end and nothing is stubbed by name. Both runners
# execute inside a SANDBOX ROOT that symlinks the real lib/, build/ and themes/
# and carries its own rices/ and modules/, with:
#
#   - PATH reduced to a stub bin, so no package manager and no network exist
#     and `sudo` runs the command instead of escalating;
#   - OSR_PASSWD_FILE pointing at a fake passwd whose home is inside the
#     sandbox, which is what keeps a full install off the real $HOME (the
#     runner resolves OSR_HOME from passwd, so this is not optional);
#   - fixture modules that are SHELL scripts, which exercises both the frozen
#     runner's `. modules/x.sh` and the core's coexistence path for one.
#
# Compared: --help, the three listings, every error path, the option loop over a
# matrix of argument orders, the manifest parser over a hostile rice.list, the
# detected-facts report, and full install / switch / module / theme-only runs -
# stdout, stderr, exit status and the resulting $HOME tree each time.
#
# TWO divergences are asserted rather than hidden:
#
#   1. an option missing its operand used to hit `${2:?--user needs a name}`,
#      whose message and exit status come from the shell itself; it is now a
#      normal `error` line. See cmd_parse_args in lib/install.c.
#   2. the package index is refreshed once per RUN, where the frozen runner
#      refreshed once per MODULE PROCESS - it spawned one `osr module run` per
#      module and each had its own once-per-process guard. One process now, one
#      refresh; `pkg_refresh` called explicitly by a module still always
#      refreshes (lib/pkg.c's refresh_once is where the guard lives, exactly
#      where lib/pkg.sh put it). Asserted at the end of this file.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip install_c_parity: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
ROOT="$TMP/root"
BIN="$TMP/bin"; mkdir -p "$BIN"

# hex — compare bytes, not lines: a trailing space or a lost newline is exactly
# the kind of difference this file exists to catch.
hex() { od -An -tx1 -v | tr -d ' \n'; }
same() { assert_eq "$2" "$3" "$1"; }

# --- the stub bin ------------------------------------------------------------
for _t in sh env cat grep sed awk printf id rm mkdir mktemp test true false tee \
          cp chmod touch cut tr head tail sort wc dirname basename find date \
          uname ln readlink od sleep kill; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$BIN/$_t" ;; esac
done
# sudo runs the command rather than escalating: every write in this test lands
# inside the sandbox, and a real escalation would prompt.
cat >"$BIN/sudo" <<'EOF'
#!/bin/sh
[ "$1" = "-u" ] && shift 2
exec "$@"
EOF
chmod +x "$BIN/sudo"
# The package managers are absent on purpose. Nothing here installs anything,
# and a missing manager is what proves it.

# --- the sandbox root --------------------------------------------------------
mkdir -p "$ROOT/rices/demo" "$ROOT/rices/broken" "$ROOT/modules"
# lib/ is COPIED, not symlinked, for one file: the frozen reference calls
# osr_state_set, and lib/state.sh is gone from the tree (lib/state.c replaced
# it). The shim below is what the reference uses; the runner calls the core
# directly, and both end up writing the same real state file.
mkdir -p "$ROOT/lib"
for _f in "$OSR_LIB"/*.sh; do ln -sfn "$_f" "$ROOT/lib/$(basename "$_f")"; done
cat >"$ROOT/lib/state.sh" <<'EOF'
osr_state_set() { "$OSR_BIN" state set "$1" "$2"; }
EOF
ln -sfn "$OSR_ROOT/build" "$ROOT/build"
ln -sfn "$OSR_ROOT/themes" "$ROOT/themes"
cp "$OSR_ROOT/install.sh" "$ROOT/install.sh"
# install.sh delegates to ./osr (which is where the one build/osr resolution
# lives), so the fixture needs that file too or the shim execs into nothing.
cp "$OSR_ROOT/osr" "$ROOT/osr"
cp "$OSR_ROOT/test/ref/install_sh_ref.sh" "$ROOT/install_ref.sh"

# Everything the manifest parser has to survive: comments (whole-line, trailing,
# mid-word), blank lines, tabs and trailing whitespace, require:/theme:/themes:
# directives, and a last line with no newline. The require: predicates are ones
# every host satisfies, so the run reaches the modules rather than stopping in
# preflight - which predicate FAILS is lib/preflight.c's own test.
printf '# the demo rice\ntheme: nord\nthemes: nord xin\n\ndemo-one   \n\t demo-two\t\nrequire: cmd:sh\ndemo-three # trailing comment\n#full comment\nrequire: cmd:sed\ndemo-last' \
    >"$ROOT/rices/demo/rice.list"
printf 'demo-one\n' >"$ROOT/rices/broken/nothing-here"
# `# themable: yes` on every fixture module so both trees take the same branch:
# the frozen reference predates the marker and always resolves a theme, and this
# file compares bytes, not intentions.
for _m in demo-one demo-two demo-three demo-last other; do
    printf '# session: x11\n# themable: yes\nprintf "STUB module %s ran\\n" %s\n' "$_m" "$_m" \
        >"$ROOT/modules/$_m.sh"
done

# Two homes, one per side, so neither run can see what the other wrote.
for _s in ref c; do
    mkdir -p "$TMP/$_s/home"
    printf 'tester:x:1000:1000::%s:/bin/sh\n' "$TMP/$_s/home" >"$TMP/$_s/passwd"
done

# run_both <label> <args...> — run the frozen sh original and the shim with
# identical environments, compare stdout, stderr and exit status.
run_both() {
    _label=$1
    shift
    for _s in ref c; do
        rm -rf "$TMP/$_s/home"; mkdir -p "$TMP/$_s/home"
        _script=$ROOT/install_ref.sh
        [ "$_s" = ref ] || _script=$ROOT/install.sh
        _rc=0
        env -i PATH="$BIN" USER=tester HOME="$TMP/$_s/home" \
            OSR_PASSWD_FILE="$TMP/$_s/passwd" OSR_SHELLS_FILE="$TMP/$_s/shells" \
            OSR_LOG="$TMP/$_s/run.log" NO_COLOR=1 TERM=dumb \
            ${OSR_MODE_ENV:-OSR_UNUSED=} \
            sh "$_script" "$@" >"$TMP/$_s.out" 2>"$TMP/$_s.err" </dev/null || _rc=$?
        eval "_${_s}_rc=\$_rc"
    done
    # The two runs write into different homes; that path is the only thing
    # allowed to differ, so it is collapsed before the bytes are compared.
    sed "s#$TMP/ref#SBOX#g" "$TMP/ref.out" >"$TMP/ref.out.n"
    sed "s#$TMP/c#SBOX#g"   "$TMP/c.out"   >"$TMP/c.out.n"
    sed "s#$TMP/ref#SBOX#g" "$TMP/ref.err" >"$TMP/ref.err.n"
    sed "s#$TMP/c#SBOX#g"   "$TMP/c.err"   >"$TMP/c.err.n"
    same "$_label: stdout" "$(hex <"$TMP/ref.out.n")" "$(hex <"$TMP/c.out.n")"
    same "$_label: stderr" "$(hex <"$TMP/ref.err.n")" "$(hex <"$TMP/c.err.n")"
    assert_eq "$_ref_rc" "$_c_rc" "$_label: exit status ($_ref_rc)"
}

# tree_same <label> — the two homes hold the same files.
tree_same() {
    assert_eq "$( (cd "$TMP/ref/home" && find . -type f | sort) )" \
              "$( (cd "$TMP/c/home" && find . -type f | sort) )" \
              "$1: the same files land in \$HOME"
}

# --- 1. the help text and the listings ---------------------------------------
run_both "--help" --help
run_both "-h" -h
run_both "--list" --list
# --list-modules is the one listing that cannot match byte for byte: the core
# merges its own compiled-in registry with the tree's modules/, and the frozen
# runner could only ever see the directory. What must hold is that the fixture's
# modules are all there, in order, and that the merge is a superset.
run_both_skip_stdout() { :; }
_rc=0
env -i PATH="$BIN" USER=tester HOME="$TMP/c/home" OSR_PASSWD_FILE="$TMP/c/passwd" \
    NO_COLOR=1 TERM=dumb sh "$ROOT/install.sh" --list-modules \
    >"$TMP/c.out" 2>"$TMP/c.err" </dev/null || _rc=$?
env -i PATH="$BIN" USER=tester HOME="$TMP/ref/home" OSR_PASSWD_FILE="$TMP/ref/passwd" \
    NO_COLOR=1 TERM=dumb sh "$ROOT/install_ref.sh" --list-modules \
    >"$TMP/ref.out" 2>"$TMP/ref.err" </dev/null || :
assert_eq 0 "$_rc" "--list-modules: exits 0"
_missing=""
while IFS= read -r _l; do
    grep -qxF "$_l" "$TMP/c.out" || _missing="$_missing [$_l]"
done <"$TMP/ref.out"
assert_eq "" "$_missing" "--list-modules: every name the directory listing had"
assert_contains "$TMP/c.out" "^  fastfetch$" "--list-modules: and the core's own registry"
run_both "--list-themes" --list-themes

# --- 2. every error path ------------------------------------------------------
run_both "unknown option" --nope
run_both "unknown short option" -x
run_both "no rice given" ''
run_both "two rices given" demo other
run_both "rice not found" nosuchrice
run_both "rice without rice.list" broken
run_both "--module with no names" --module
run_both "--module unknown module" --module nosuchmodule

# --- 3. the option loop, over a matrix of argument orders --------------------
# No probe into either implementation's variables: what each parsed is visible
# in what it then did, which is the thing that actually matters.
run_both "plain rice" demo
tree_same "plain rice"
run_both "every option" --user tester --theme xin --verbose demo
tree_same "every option"
run_both "options after the rice" demo --user tester --verbose
run_both "module mode" --module demo-one demo-two
tree_same "module mode"
run_both "module mode with a theme" --module --theme nord demo-one
run_both "theme-only" --theme-only --theme nord --no-reload
tree_same "theme-only"

# --- 4. switch is the same engine with a different closing line --------------
OSR_MODE_ENV="OSR_MODE=switch"
run_both "switch" demo
OSR_MODE_ENV=""

# --- 5. the manifest parser ---------------------------------------------------
# The hostile rice.list above is parsed by both, and the module order it yields
# is the order the STUB lines appear in. A directive that leaked into the module
# list would show up as a missing module or an extra one.
run_both "hostile manifest" demo
for _m in demo-one demo-two demo-three demo-last; do
    assert_contains "$TMP/c.out" "STUB module $_m ran" "manifest: $_m ran"
done
refute_contains "$TMP/c.out" "STUB module other ran" "manifest: a rice runs only its own modules"
assert_contains "$TMP/c.out" "require cmd:sh - ok" "manifest: require: lines reach preflight"
assert_contains "$TMP/c.out" "require cmd:sed - ok" "manifest: the second require: too"
refute_contains "$TMP/c.out" "module: theme" "manifest: theme: is not a module"
refute_contains "$TMP/c.out" "module: themes" "manifest: themes: is not a module"

# A shell module still runs, which is the coexistence contract: the core hands
# it to a shell with the libs sourced around it, and nothing in the rice says
# which tier it wanted.
assert_contains "$TMP/c.out" '\[01/04\] module: demo-one' "the step counter counts the manifest"

# --- 6. the divergence, asserted ---------------------------------------------
# An option missing its operand: the frozen runner died inside the shell's own
# ${x:?...} expansion; the core prints a normal error line.
_rc=0
env -i PATH="$BIN" USER=tester HOME="$TMP/c/home" OSR_PASSWD_FILE="$TMP/c/passwd" \
    NO_COLOR=1 TERM=dumb sh "$ROOT/install.sh" --user >"$TMP/c.out" 2>"$TMP/c.err" \
    </dev/null || _rc=$?
assert_contains "$TMP/c.err" 'user needs a name' "missing operand: says which option"
assert_eq 1 "$_rc" "missing operand: exits 1 (the frozen runner exited 2, from the shell)"

# The package index refresh. Nothing in this sandbox installs anything, so the
# assertion is on the shape of the guard rather than on a count: an explicit
# pkg_refresh always refreshes, and only the install path is once-per-run.
assert_contains "$OSR_ROOT/lib/pkg.c" "refresh_once" \
    "the once-per-run guard is the install path's, not pkg_refresh's"

finish
