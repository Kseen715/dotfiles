#!/bin/sh
# Proves install.sh (now driving lib/install.c for its text and its
# decisions) behaves byte-for-byte like the pure-sh runner, frozen at
# test/ref/install_sh_ref.sh.
#
# Both are run inside a FIXTURE tree: lib/ holds the real ui/log/cbuild plus
# STUB versions of everything install.sh calls into (osr_detect,
# osr_resolve_user, osr_resolve_theme, pkg_install, the state writers), so a
# whole run is hermetic - no distro detection, no sudo, no packages, no writes
# outside the tree. The stubs also act as probes: they print the variables
# install.sh had reached that point with, which is how the option loop and the
# manifest parser get compared without either implementation being asked to
# explain itself.
#
# Compared: --help, the three listings, every error path, the parsed variables
# over a matrix of argument orders, the manifest parser over hostile rice.list
# fixtures, the detected-facts report over several fact sets, and a full
# install/switch/module run end to end (stdout, stderr and exit status each
# time).
#
# The ONE known divergence is asserted rather than hidden: an option missing
# its operand used to hit `${2:?--user needs a name}`, whose message and exit
# status come from the shell itself; it is now a normal `error` line. See
# cmd_parse_args in lib/install.c.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

# A stub sudo, because both halves of this comparison need one and for
# different reasons: install.sh warms a sudo ticket when stdin is a terminal
# (the pty case below), and `osr state set` escalates to $OSR_USER to write the
# state file — where the frozen runner called as_user, which the fixture could
# stub as a shell function and this cannot be. Ticket warm-up still fails (it
# is best-effort by design, so that path stays exercised); `-u <user> <cmd>`
# runs the command as us, which is what the fixture's as_user stub did.
mkdir -p "$TMP/bin"
cat >"$TMP/bin/sudo" <<'EOF'
#!/bin/sh
case "$1" in
    -v|-n) exit 1 ;;
    -u)    shift 2; exec "$@" ;;
esac
exec "$@"
EOF
chmod +x "$TMP/bin/sudo"
PATH="$TMP/bin:$PATH"; export PATH

# Three things are normalized away, each because it is a DELIBERATE change
# since the frozen reference was written; each is then asserted on its own
# below, so the change stays visible instead of being swallowed here:
#
#   1. the tree root, which differs between the two runs and shows up in paths
#   2. `STUB state_set k=v`, the fixture's stand-in for lib/state.sh - that
#      file is gone and install.sh calls `osr state set`, which writes a real
#      file (checked after the full run)
#   3. the C modules in `--list-modules`, which the frozen install.sh could
#      not know about
hex() {
    sed "s|$TMP/ref|TREE|g; s|$TMP/c|TREE|g
         /^STUB state_set /d
         /^  docker$/d
         /^  fastfetch$/d
         /^  flameshot$/d" | od -An -tx1 | tr -d ' \n'
}

same() {
    if [ "$2" = "$3" ]; then
        ok "$1"
    else
        fail "$1"
        printf '    ref: %s\n    c  : %s\n' "$2" "$3" >&2
    fi
}

# Facts the report lines are built from. Exported variables are shell
# variables to the sh original and environment to the C helper, so one set
# feeds both. Every name install.sh reads must be present: it runs under
# `set -u`.
FACTS="OSR_DISTRO=arch OSR_VERSION_ID= OSR_CODENAME= OSR_VERSION= OSR_ID_LIKE=
       OSR_PKG=pacman OSR_INIT=systemd OSR_USER=alice OSR_HOME=/home/alice
       OSR_CPU_MODEL=Ryzen_7 OSR_CPU_ARCH=x86_64 OSR_CPU_CORES=8 OSR_CPU_THREADS=16
       OSR_VIRT=none OSR_RAM_TOTAL=32G OSR_RAM_TYPE=DDR4 OSR_RAM_SPEED=3200MT/s
       OSR_RAM_STICKS=2 OSR_RAM_CHANNELS=2 OSR_GPU_MODEL=RX_6800 OSR_GPU_VENDOR=amd
       OSR_NPU_VENDOR= OSR_DEFAULT_THEME=nord OSR_THEME=nord"

# make_tree <dir> — a miniature os-rice: real ui/log/cbuild + C sources,
# stubbed everything else, one rice, two modules.
make_tree() {
    _t=$1
    mkdir -p "$_t/lib" "$_t/rices/demo" "$_t/rices/broken" "$_t/modules" "$_t/build"
    for _f in ui.sh log.sh; do
        ln -sfn "$OSR_LIB/$_f" "$_t/lib/$_f"
    done
    # The tree's own build/ is the real one: the harness core is a binary now,
    # not something a shim compiles per checkout.
    ln -sfn "$OSR_ROOT/build" "$_t/build"
    ln -sfn "$OSR_ROOT/themes" "$_t/themes"
    cp "$OSR_ROOT/install.sh" "$_t/install.sh"
    cp "$OSR_ROOT/test/ref/install_sh_ref.sh" "$_t/install_ref.sh"

    # --- stub libs: no detection, no packages, no writes ---------------------
    cat >"$_t/lib/detect.sh" <<'EOF'
# The facts arrive in the environment (see FACTS); detection itself is a
# no-op, and probe=parse stops the run right here, where install.sh has
# finished parsing its arguments and nothing else has happened yet.
osr_detect() {
    if [ "${OSR_PROBE:-}" = parse ]; then
        printf 'user=[%s] theme=[%s] module=[%s] themeonly=[%s] noreload=[%s] pos=[%s] verbose=[%s]\n' \
            "$OSR_ARG_USER" "$OSR_ARG_THEME" "$OSR_MODULE_MODE" "$OSR_THEME_ONLY" \
            "$OSR_NO_RELOAD" "$OSR_POS" "${OSR_VERBOSE:-}"
        exit 0
    fi
}
osr_detect_ram() { :; }
EOF
    cat >"$_t/lib/user.sh" <<'EOF'
osr_resolve_user() { :; }
as_user() { "$@"; }
as_root() { "$@"; }
EOF
    cat >"$_t/lib/pkg.sh" <<'EOF'
pkg_install() { printf 'STUB pkg_install %s\n' "$*"; }
EOF
    cat >"$_t/lib/theme.sh" <<'EOF'
# probe=manifest stops right after the manifest was parsed and before any
# theme work, which is where OSR_MODULES/OSR_REQUIRES are final.
osr_resolve_theme() {
    if [ "${OSR_PROBE:-}" = manifest ]; then
        printf 'modules=[%s] requires=[%s]\n' "$OSR_MODULES" "$OSR_REQUIRES"
        exit 0
    fi
    if [ "${OSR_PROBE:-}" = report ]; then exit 0; fi
    OSR_THEME=${1:-nord}
    export OSR_THEME
}
osr_unset_theme() { OSR_THEME=""; OSR_THEME_DIR=""; export OSR_THEME OSR_THEME_DIR; }
osr_rice_default_theme() { printf 'nord'; }
osr_themes() { printf 'nord xin\n'; }
osr_theme_meta() { printf 'a stub theme'; }
osr_apply_theme_configs() { printf 'STUB apply_theme_configs\n'; }
osr_apply_theme() { printf 'STUB apply_theme %s\n' "$*"; OSR_THEME=$1; export OSR_THEME; }
EOF
    cat >"$_t/lib/config.sh" <<'EOF'
apply_wallpaper() { printf 'STUB apply_wallpaper\n'; }
EOF
    # lib/state.sh is gone from the tree; the frozen reference still calls
    # osr_state_set, so the STUB below is what IT uses. install.sh calls the
    # core instead, which writes the real file under this tree's HOME.
    cat >"$_t/lib/state.sh" <<'EOF'
osr_state_set() { printf 'STUB state_set %s=%s\n' "$1" "$2"; }
EOF
    mkdir -p "$_t/home"
    cat >"$_t/lib/preflight.sh" <<'EOF'
osr_preflight() { printf 'STUB preflight %s\n' "$*"; }
EOF
    cat >"$_t/lib/reload.sh" <<'EOF'
osr_reload_all() { printf 'STUB reload_all\n'; }
EOF

    # --- fixture rice + modules ---------------------------------------------
    # Everything the manifest parser has to survive: comments (whole-line,
    # trailing, mid-word), blank lines, tabs and trailing whitespace,
    # require:/theme:/themes: directives, and a last line with no newline.
    printf '# the demo rice\ntheme: nord\nthemes: nord xin\n\nzsh   \n\t foot\t\nrequire: gpu:amd\nbar # trailing comment\n#full comment\nrequire: ram:8G\nlast-no-newline' \
        >"$_t/rices/demo/rice.list"
    printf 'zsh\n' >"$_t/rices/broken/nothing-here"
    # `# themable: yes` on every fixture module so both trees take the same
    # branch: the frozen reference predates the marker and always resolves a
    # theme, and this file compares bytes, not intentions. The branch the marker
    # added -- a module set that reads no theme resolves none, and asks nothing
    # -- is the subject of test/unit/module_themable.sh instead.
    for _m in zsh foot bar last-no-newline other; do
        printf '# session: x11\n# themable: yes\nprintf "STUB module %s ran\\n" %s\n' "$_m" "$_m" >"$_t/modules/$_m.sh"
    done
}

# run_both <label> <probe> <args...> — run the frozen sh original and the
# current install.sh with identical environments, compare all three outputs.
run_both() {
    _label=$1; _probe=$2
    shift 2
    _rrc=0
    # shellcheck disable=SC2086  # FACTS is a deliberate list of assignments
    env $FACTS OSR_PROBE="$_probe" NO_COLOR=1 TERM=dumb OSR_HOME="$TMP/ref/home" \
        sh "$TMP/ref/install_ref.sh" "$@" >"$TMP/ref.out" 2>"$TMP/ref.err" </dev/null || _rrc=$?
    _crc=0
    # shellcheck disable=SC2086
    env $FACTS OSR_PROBE="$_probe" NO_COLOR=1 TERM=dumb OSR_HOME="$TMP/c/home" \
        sh "$TMP/c/install.sh" "$@" >"$TMP/c.out" 2>"$TMP/c.err" </dev/null || _crc=$?
    same "$_label: stdout" "$(hex <"$TMP/ref.out")" "$(hex <"$TMP/c.out")"
    same "$_label: stderr" "$(hex <"$TMP/ref.err")" "$(hex <"$TMP/c.err")"
    assert_eq "$_rrc" "$_crc" "$_label: exit status ($_rrc)"
}

make_tree "$TMP/ref"
make_tree "$TMP/c"

# --- 1. the help text and the listings ---------------------------------------
run_both "--help" '' --help
run_both "-h" '' -h
run_both "--list" '' --list
run_both "--list-modules" '' --list-modules
run_both "--list-themes" '' --list-themes

# --- 2. every error path ------------------------------------------------------
run_both "unknown option" '' --nope
run_both "unknown short option" '' -x
run_both "no rice given" '' ''
run_both "two rices given" '' demo other
run_both "rice not found" '' nosuchrice
run_both "rice without rice.list" '' broken
run_both "--module with no names" '' --module
run_both "--module unknown module" '' --module nosuchmodule

# --- 3. the option loop -------------------------------------------------------
# probe=parse prints what install.sh parsed, at the moment it finished.
run_both "parse: plain rice" parse demo
run_both "parse: every option" parse --user alice --theme xin --verbose demo
run_both "parse: options after the rice" parse demo --user alice --verbose
run_both "parse: module mode" parse --module zsh foot
run_both "parse: theme-only" parse --theme-only --theme nord --no-reload
run_both "parse: repeated options (last wins)" parse --theme a --theme b demo
run_both "parse: value that looks like an option" parse --user --verbose demo
run_both "parse: empty positional" parse demo ''
run_both "parse: odd characters" parse --user "o'brien" --theme 'a b' "ri ce"

# --- 4. the manifest parser ---------------------------------------------------
run_both "manifest: the fixture rice" manifest demo
# The same parser, over a rice.list that is nothing but noise.
for _fx in 'empty' 'comments' 'spaces' 'requires'; do
    case "$_fx" in
        empty)    _body='' ;;
        comments) _body='# one\n   # two\n\t\n\n' ;;
        spaces)   _body='  zsh  \n\t\tfoot\t\n   \n bar' ;;
        requires) _body='require: a\nrequire:b\nrequire:   c  \nzsh\n' ;;
    esac
    # shellcheck disable=SC2059  # the fixture body IS the format string
    printf "$_body" >"$TMP/ref/rices/demo/rice.list"
    # shellcheck disable=SC2059
    printf "$_body" >"$TMP/c/rices/demo/rice.list"
    run_both "manifest: $_fx" manifest demo
done
make_tree "$TMP/ref"   # restore the standard fixture rice
make_tree "$TMP/c"

# --- 5. the detected-facts report --------------------------------------------
# probe=report runs past the report lines and stops at the theme resolution.
run_both "report: full fact set" report demo
_saved=$FACTS
# A minimal machine: no version fields, no model, no RAM detail, no GPU.
FACTS="OSR_DISTRO=alpine OSR_VERSION_ID= OSR_CODENAME= OSR_VERSION= OSR_ID_LIKE=
       OSR_PKG=apk OSR_INIT=openrc OSR_USER=root OSR_HOME=/root
       OSR_CPU_MODEL= OSR_CPU_ARCH=aarch64 OSR_CPU_CORES=0 OSR_CPU_THREADS=0
       OSR_VIRT=docker OSR_RAM_TOTAL= OSR_RAM_TYPE= OSR_RAM_SPEED=
       OSR_RAM_STICKS=1 OSR_RAM_CHANNELS=0 OSR_GPU_MODEL= OSR_GPU_VENDOR=
       OSR_NPU_VENDOR= OSR_DEFAULT_THEME=nord OSR_THEME=nord"
run_both "report: bare machine" report demo
# Everything present, including the fields that only appear when set.
FACTS="OSR_DISTRO=debian OSR_VERSION_ID=12 OSR_CODENAME=bookworm OSR_VERSION=12_bookworm
       OSR_ID_LIKE=ubuntu OSR_PKG=apt OSR_INIT=systemd OSR_USER=bob OSR_HOME=/home/bob
       OSR_CPU_MODEL=i7-1165G7 OSR_CPU_ARCH=x86_64 OSR_CPU_CORES=4 OSR_CPU_THREADS=8
       OSR_VIRT=kvm OSR_RAM_TOTAL=16G OSR_RAM_TYPE=LPDDR4X OSR_RAM_SPEED=4267MT/s
       OSR_RAM_STICKS=2 OSR_RAM_CHANNELS=2 OSR_GPU_MODEL=Iris_Xe OSR_GPU_VENDOR=intel
       OSR_NPU_VENDOR=intel OSR_DEFAULT_THEME=nord OSR_THEME=nord"
run_both "report: every facet detected" report demo
# threads == cores must NOT print a threads= field (it is noise).
FACTS="$(printf '%s' "$FACTS" | sed 's/OSR_CPU_THREADS=8/OSR_CPU_THREADS=4/')"
run_both "report: no SMT (threads == cores)" report demo
FACTS=$_saved

# --- 6. whole runs ------------------------------------------------------------
# Nothing is stubbed out between here and the final [DONE] line: the module
# loop sources the fixture modules, the step counter counts them, and the
# closing sentence depends on how the run was started.
run_both "full: rice install" '' demo
run_both "full: rice install, verbose" '' --verbose demo
run_both "full: module mode" '' --module zsh foot
run_both "full: theme-only" '' --theme-only --theme xin
run_both "full: theme-only, no reload" '' --theme-only --theme xin --no-reload

# The state the run recorded: the sh version called a shell function (the
# fixture stubbed it), the C one writes the file itself. Same three keys.
assert_eq "rice=demo" "$(grep '^rice=' "$TMP/c/home/.config/osr/state" 2>/dev/null)" \
    "full: the run recorded its rice"
assert_eq "theme=nord" "$(grep '^theme=' "$TMP/c/home/.config/osr/state" 2>/dev/null)" \
    "full: ...and its theme"
if grep -qE '^applied=[0-9]+$' "$TMP/c/home/.config/osr/state" 2>/dev/null; then
    ok "full: ...and when"
else
    fail "full: applied= timestamp missing from the state file"
fi

# --list-modules now merges the modules the core implements in C with the
# shell ones, in one alphabetical list. The frozen runner listed only *.sh.
_mods=$(env $FACTS sh "$TMP/c/install.sh" --list-modules </dev/null | sed 1d)
printf '%s\n' "$_mods" | grep -q '^  docker$' \
    && ok "--list-modules: lists the C modules too" \
    || fail "--list-modules: C modules missing"
printf '%s\n' "$_mods" | sort -c 2>/dev/null \
    && ok "--list-modules: still one alphabetical list" \
    || fail "--list-modules: not sorted"

# `switch` is the same engine with a different closing sentence (§6).
_rrc=0
# shellcheck disable=SC2086
env $FACTS OSR_MODE=switch NO_COLOR=1 TERM=dumb OSR_HOME="$TMP/ref/home" \
    sh "$TMP/ref/install_ref.sh" demo >"$TMP/ref.out" 2>"$TMP/ref.err" </dev/null || _rrc=$?
_crc=0
# shellcheck disable=SC2086
env $FACTS OSR_MODE=switch NO_COLOR=1 TERM=dumb OSR_HOME="$TMP/c/home" \
    sh "$TMP/c/install.sh" demo >"$TMP/c.out" 2>"$TMP/c.err" </dev/null || _crc=$?
same "full: switch mode stdout" "$(hex <"$TMP/ref.out")" "$(hex <"$TMP/c.out")"
assert_eq "$_rrc" "$_crc" "full: switch mode exit status"

# --- 7. on a terminal ---------------------------------------------------------
# The whole point of the palette: a colored run must be identical too.
if command -v script >/dev/null 2>&1; then
    # One line, because this one goes through a command STRING, not argv.
    _facts1=$(printf '%s' "$FACTS" | tr '\n' ' ')
    _r=$(script -q -c "env $_facts1 TERM=xterm COLUMNS=100 OSR_HOME=$TMP/ref/home sh $TMP/ref/install_ref.sh demo" /dev/null | hex)
    _c=$(script -q -c "env $_facts1 TERM=xterm COLUMNS=100 OSR_HOME=$TMP/c/home sh $TMP/c/install.sh demo" /dev/null | hex)
    same "pty: colored full run" "$_r" "$_c"
else
    ok "pty check skipped (no script(1))"
fi

# --- 8. the documented divergence --------------------------------------------
# An option missing its operand: sh hit `${2:?...}` (the shell's own
# diagnostic, exit 2), C reports it through error() (exit 1). Asserted, so it
# stays a decision rather than becoming a surprise.
_crc=0
# shellcheck disable=SC2086
env $FACTS NO_COLOR=1 sh "$TMP/c/install.sh" --user >"$TMP/c.out" 2>"$TMP/c.err" </dev/null || _crc=$?
assert_eq "1" "$_crc" "missing operand: exits 1 (was the shell's 2)"
assert_eq "[ERROR] --user needs a name" "$(cat "$TMP/c.err")" "missing operand: reported via error()"

finish
