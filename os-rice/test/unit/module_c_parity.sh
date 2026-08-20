#!/bin/sh
# Proves the Linux modules written in C (the POSIX branch of modules/*.c, run by
# `osr module run <name>`) do what the shell modules they replaced did, frozen
# at test/ref/<name>_sh_ref.sh.
#
# Hermetic: PATH is reduced to a stub bin/, so "is dpkg installed", "does
# groupadd exist" and every install command are properties of the scenario, not
# of this machine. Each stub logs its argv; the comparison is that log, which is
# the only thing a module actually does to a box - what did it decide to run,
# with which arguments, in which order.
#
# The sh side runs with the real lib/*.sh (pkg.sh, service.sh, user.sh) around
# it; the C side runs the core. Both therefore go through their OWN package
# layer, which is exactly what is being compared: the C tier's native path
# (lib/module.c) against lib/pkg.sh's.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd); export OSR_DOTFILES
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
BIN="$TMP/bin"; mkdir -p "$BIN"
LOG="$TMP/log"; export LOG

# stub <name> [exit-code] — a fake tool that records how it was called.
stub() {
    rm -f "$BIN/$1"
    cat >"$BIN/$1" <<EOF
#!/bin/sh
printf '%s %s\n' "$1" "\$*" >>"\$LOG"
exit ${2:-0}
EOF
    chmod +x "$BIN/$1"
}
# The tools the sh libs themselves need to run at all.
for _t in sh env cat cut grep sed awk tr head tail printf id mktemp rm cp mkdir tee sort od wc dirname basename sleep kill; do
    _p=$(command -v "$_t" 2>/dev/null) && ln -sf "$_p" "$BIN/$_t"
done

# The facts both sides read. OSR_LOG points into the sandbox so the step window
# never touches the real run log.
FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB OSR_DOTFILES=$OSR_DOTFILES
       OSR_PKG=apt OSR_INIT=systemd OSR_DISTRO=ubuntu OSR_ID_LIKE=debian
       OSR_CODENAME=noble OSR_VERSION_ID=24.04 OSR_ARCH=x86_64
       OSR_USER=tester OSR_HOME=$TMP/home OSR_THEME=nord NO_COLOR=1 TERM=dumb
       OSR_LOG=$TMP/run.log OSR_VERBOSE=1"

# sudo: the one tool both sides really do call. It records the escalation and
# then runs the command, so `as_root apt-get ...` in sh and osr_run_root() in C
# leave the same two lines.
cat >"$BIN/sudo" <<'EOF'
#!/bin/sh
printf 'sudo %s\n' "$*" >>"$LOG"
[ "$1" = "-u" ] && shift 2
exec "$@"
EOF
chmod +x "$BIN/sudo"

# One line, because the pty case below goes through a command STRING.
FACTS_1LINE=$(printf '%s' "$FACTS" | tr '\n' ' ')

# run_sh <module> — the frozen shell module, with the real libs around it.
run_sh() {
    : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" $FACTS HOME="$TMP/home" sh -c '
        . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
        for l in detect user pkg service; do . "$OSR_LIB/$l.sh"; done
        . "$1"' _ "$OSR_ROOT/test/ref/$1_sh_ref.sh" >"$TMP/sh.out" 2>&1 || :
    cp "$LOG" "$TMP/sh.log"
}

# run_c <module> — the C module, through the core.
run_c() {
    : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" $FACTS HOME="$TMP/home" OSR_BIN="$OSR_BIN" \
        "$OSR_BIN" module run "$1" >"$TMP/c.out" 2>&1 || :
    cp "$LOG" "$TMP/c.log"
}

# normalize <file> — drop `id -u`, which is only how the SHELL as_root asks
# whether it is already root; the C tier calls getuid(). Same decision, one
# fewer process, and not a difference in what the module did to the box.
normalize() { grep -v '^id -u$' "$1" || :; }

# ..._env variants: the same two runners with extra facts (a sandboxed
# dotfiles tree and theme, for the config-layer cases).
run_sh_env() {
    : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" $FACTS $2 HOME="$TMP/home" sh -c '
        . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
        for l in detect user net pkg service config theme build; do . "$OSR_LIB/$l.sh"; done
        . "$1"' _ "$OSR_ROOT/test/ref/$1_sh_ref.sh" >"$TMP/sh.out" 2>&1 || :
    cp "$LOG" "$TMP/sh.log"
}
run_c_env() {
    : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" $FACTS $2 HOME="$TMP/home" OSR_BIN="$OSR_BIN" \
        "$OSR_BIN" module run "$1" >"$TMP/c.out" 2>&1 || :
    cp "$LOG" "$TMP/c.log"
}

compare() {
    # An empty log on both sides would "agree" without either module having
    # done anything, which is exactly the bug this guard exists for.
    if [ ! -s "$TMP/sh.log" ]; then
        fail "$1 (the sh reference did nothing - the sandbox is wrong)"
        return
    fi
    if [ "$(normalize "$TMP/sh.log")" = "$(normalize "$TMP/c.log")" ]; then
        ok "$1"
    else
        fail "$1"
        diff -u "$TMP/sh.log" "$TMP/c.log" >&2 || :
    fi
}

# --- 1. flameshot: nothing installed yet -------------------------------------
stub dpkg 1            # nothing is installed
stub apt-get 0
run_sh flameshot; run_c flameshot
compare "flameshot: installs the four packages the same way"
assert_contains "$TMP/c.log" "apt-get install -y -q -o Dpkg::Use-Pty=0 flameshot maim slop xclip" \
    "flameshot: one batched apt-get, not four"

# --- 2. flameshot: already installed (the §2 rerun) --------------------------
stub dpkg 0            # everything is installed
run_sh flameshot; run_c flameshot
compare "flameshot: a rerun installs nothing"
refute_contains "$TMP/c.log" "apt-get install" "flameshot: no install on a rerun"

# --- 3. pkgmap resolution: a name that differs on this manager ---------------
# `fd = fd-find` in apt.map — the C tier must resolve it like _pkgmap_one did.
stub dpkg 1
_r=$(env -i PATH="$BIN" LOG="$LOG" $FACTS sh -c '. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
    . "$OSR_LIB/detect.sh"; . "$OSR_LIB/pkg.sh"; _pkgmap_one fd')
_c=$(env -i PATH="$BIN" LOG="$LOG" $FACTS "$OSR_BIN" module pkgmap fd 2>/dev/null || printf 'MISSING')
assert_eq "$_r" "$_c" "pkgmap: fd resolves the same on apt"
for _n in build dev-headers zsh btop nosuchpackage; do
    _r=$(env -i PATH="$BIN" LOG="$LOG" $FACTS sh -c '. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
        . "$OSR_LIB/detect.sh"; . "$OSR_LIB/pkg.sh"; _pkgmap_one "$1"' _ "$_n")
    _c=$(env -i PATH="$BIN" LOG="$LOG" $FACTS "$OSR_BIN" module pkgmap "$_n")
    assert_eq "$_r" "$_c" "pkgmap: $_n resolves the same"
done

# --- 3b. fastfetch: package + the themed config layer -----------------------
# The interesting half is the config: fastfetch ships ONE config.jsonc, so the
# theme's own file wins, else the dotfiles .tmpl is rendered with the theme's
# palette. The rendered bytes are compared, not just the commands.
mkdir -p "$TMP/home" "$TMP/df/fastfetch" "$TMP/themes/nord/config/fastfetch"
# On pacman, `fastfetch` is a bare passthrough; on apt/noble it is a `source:`
# provider row, which really downloads a .deb - not something a hermetic test
# can run. The provider path gets its own scenario at (d) below; these compare
# the config layering, which is the half that is new in C.
FF_ENV="OSR_DOTFILES=$TMP/df OSR_THEME=nord OSR_THEME_DIR=$TMP/themes/nord
        OSR_PKG=pacman OSR_CODENAME= OSR_VERSION_ID="
# pacman: not installed (`-Q` fails), installs fine (everything else). One
# blanket exit code would abort the run before the config layer, which is the
# half being compared here.
rm -f "$BIN/pacman"
cat >"$BIN/pacman" <<'EOF'
#!/bin/sh
printf 'pacman %s\n' "$*" >>"$LOG"
[ "$1" = "-Q" ] && exit 1
exit 0
EOF
chmod +x "$BIN/pacman"

# (a) the theme ships the file itself
printf 'THEME OWNED
' >"$TMP/themes/nord/config/fastfetch/config.jsonc"
printf 'DOTFILES BASE
' >"$TMP/df/fastfetch/config.jsonc"
stub dpkg 1; stub apt-get 0
run_sh_env fastfetch "$FF_ENV"; cp "$TMP/home/.config/fastfetch/config.jsonc" "$TMP/ff.sh" 2>/dev/null || :
rm -rf "$TMP/home"
run_c_env fastfetch "$FF_ENV"; cp "$TMP/home/.config/fastfetch/config.jsonc" "$TMP/ff.c" 2>/dev/null || :
compare "fastfetch: theme-owned config"
assert_eq "$(cat "$TMP/ff.sh" 2>/dev/null)" "$(cat "$TMP/ff.c" 2>/dev/null)" \
    "fastfetch: the installed file is the theme's own"

# (b) no theme file: the dotfiles template, rendered with the palette
rm -f "$TMP/themes/nord/config/fastfetch/config.jsonc"
cp "$OSR_ROOT/themes/nord/theme.list" "$TMP/themes/nord/theme.list"
printf 'bg={{background}} fg={{foreground_rgb}} dec={{accent_dec}} sgr={{accent_sgr}}
name={{THEME}} missing={{nosuchkey}} wall={{WALLPAPER_PATH}}
' \
    >"$TMP/df/fastfetch/config.jsonc.tmpl"
rm -rf "$TMP/home"
run_sh_env fastfetch "$FF_ENV"; cp "$TMP/home/.config/fastfetch/config.jsonc" "$TMP/ff.sh" 2>/dev/null || :
rm -rf "$TMP/home"
run_c_env fastfetch "$FF_ENV"; cp "$TMP/home/.config/fastfetch/config.jsonc" "$TMP/ff.c" 2>/dev/null || :
assert_eq "$(cat "$TMP/ff.sh" 2>/dev/null)" "$(cat "$TMP/ff.c" 2>/dev/null)" \
    "fastfetch: the rendered template is byte-identical"
assert_contains "$TMP/ff.c" "wall={{WALLPAPER_PATH}}" \
    "fastfetch: {{WALLPAPER_PATH}} is left for the second pass"
assert_contains "$TMP/ff.c" "missing={{nosuchkey}}" \
    "fastfetch: a key the theme lacks is left alone"
assert_eq "$(grep -c 'defines no nosuchkey' "$TMP/sh.out" 2>/dev/null)" \
    "$(grep -c 'defines no nosuchkey' "$TMP/c.out" 2>/dev/null)" \
    "fastfetch: both warn about the key the theme lacks"

# (c) neither: the dotfiles base file, unrendered
rm -f "$TMP/df/fastfetch/config.jsonc.tmpl"
rm -rf "$TMP/home"
run_sh_env fastfetch "$FF_ENV"; cp "$TMP/home/.config/fastfetch/config.jsonc" "$TMP/ff.sh" 2>/dev/null || :
rm -rf "$TMP/home"
run_c_env fastfetch "$FF_ENV"; cp "$TMP/home/.config/fastfetch/config.jsonc" "$TMP/ff.c" 2>/dev/null || :
assert_eq "DOTFILES BASE" "$(cat "$TMP/ff.c" 2>/dev/null)" "fastfetch: falls back to the dotfiles base"
assert_eq "$(cat "$TMP/ff.sh" 2>/dev/null)" "$(cat "$TMP/ff.c" 2>/dev/null)" "fastfetch: same fallback both ways"

# (d) the provider row: `fastfetch@noble = source:provide_fastfetch_deb` in
# apt.map. The C tier implements the native path only and hands such a row back
# to lib/pkg.sh, which owns the builders in lib/build.sh - so the same builder
# runs either way. This sandbox has no downloader, so what is compared is that
# both reach the builder and both fail there, identically.
: >"$LOG"
stub dpkg 1
_prov=$(env -i PATH="$BIN" LOG="$LOG" $FACTS HOME="$TMP/home" \
    "$OSR_BIN" module pkgmap fastfetch)
assert_eq "source:provide_fastfetch_deb" "$_prov" "fastfetch: apt.map yields a provider row on noble"
rm -rf "$TMP/home"
run_sh_env fastfetch "OSR_DOTFILES=$TMP/df OSR_THEME=nord OSR_THEME_DIR=$TMP/themes/nord"
rm -rf "$TMP/home"
run_c_env fastfetch "OSR_DOTFILES=$TMP/df OSR_THEME=nord OSR_THEME_DIR=$TMP/themes/nord"
assert_contains "$TMP/sh.out" "building fastfetch from source" \
    "provider row: the sh tier reaches the builder"
assert_contains "$TMP/c.out" "building fastfetch from source" \
    "provider row: so does the C tier, through the same builder"
compare "provider row: identical command sequence either way"

# --- 4. docker: the full path (package, group, membership, service) ----------
stub dpkg 1
stub getent 1          # no docker group yet
stub groupadd 0
stub usermod 0
stub id 0
stub systemctl 1       # not enabled yet -> enable --now
run_sh docker; run_c docker
compare "docker: package + group + membership + service"
assert_contains "$TMP/c.log" "groupadd docker" "docker: creates the group"
assert_contains "$TMP/c.log" "usermod -aG docker tester" "docker: adds the user"
assert_contains "$TMP/c.log" "systemctl enable --now docker" "docker: enables the daemon"

# --- 5. docker on a busybox box: addgroup instead of groupadd ----------------
rm -f "$BIN/groupadd" "$BIN/usermod" "$BIN/getent"
stub addgroup 0
run_sh docker; run_c docker
compare "docker: busybox tooling (addgroup)"

# --- 6. docker, already done: the §2 rerun ----------------------------------
stub dpkg 0
stub getent 0          # group exists
stub systemctl 0       # enabled AND active
rm -f "$BIN/addgroup"
stub usermod 0
stub groupadd 0
# `id -nG tester` says the user is already in it
cat >"$BIN/id" <<'EOF'
#!/bin/sh
printf 'id %s\n' "$*" >>"$LOG"
[ "$1" = "-nG" ] && { echo "tester docker sudo"; exit 0; }
echo 1000
EOF
chmod +x "$BIN/id"
run_sh docker; run_c docker
compare "docker: a fully-applied box changes nothing"
refute_contains "$TMP/c.log" "groupadd" "docker rerun: no group creation"
refute_contains "$TMP/c.log" "usermod" "docker rerun: no membership change"

# --- 7. the live step window, on a real terminal -----------------------------
# Everything above runs with OSR_VERBOSE set, which takes the plain streamed
# path - so none of it exercises the window that repaints while a step runs,
# and none of it could catch the bug this section exists for: the spin loop
# must REAP its own child. An exited child stays a pid until someone waits on
# it, so a loop that asks "does this pid still exist" (which is what the shell
# version could do, because the shell reaps in the background) never ends.
if command -v script >/dev/null 2>&1 && command -v timeout >/dev/null 2>&1; then
    # A package manager that takes a moment and prints as it goes, so the
    # window actually paints more than one frame.
    rm -f "$BIN/pacman"
    cat >"$BIN/pacman" <<'EOF'
#!/bin/sh
printf 'pacman %s
' "$*" >>"$LOG"
[ "$1" = "-Q" ] && exit 1
i=1
while [ "$i" -le 4 ]; do echo "installing chunk $i"; sleep 0.15; i=$((i + 1)); done
exit 0
EOF
    chmod +x "$BIN/pacman"
    rm -f "$BIN/sudo"
    printf '#!/bin/sh
[ "$1" = "-u" ] && shift 2
exec "$@"
' >"$BIN/sudo"
    chmod +x "$BIN/sudo"
    rm -rf "$TMP/home"; mkdir -p "$TMP/home"
    # One line: this goes through `script -c`, so a newline would end the
    # command and leave the rest of the environment unset.
    _live="env -i PATH=$BIN LOG=$LOG HOME=$TMP/home OSR_BIN=$OSR_BIN $FACTS_1LINE OSR_PKG=pacman OSR_CODENAME= OSR_VERSION_ID= OSR_VERBOSE= TERM=xterm $OSR_BIN module run flameshot"
    _rc=0
    timeout 25 script -q -c "$_live" /dev/null >"$TMP/live.out" 2>&1 || _rc=$?
    if [ "$_rc" -eq 124 ]; then
        fail "live window: the step never finished (the spinner hung)"
    else
        ok "live window: the step finished"
    fi
    assert_contains "$TMP/live.out" '\[ok\] Installing screenshot tools' \
        "live window: collapses to one [ok] line"
    assert_contains "$TMP/live.out" 'installing chunk' \
        "live window: showed the command's output while it ran"
else
    ok "live window checks skipped (no script(1)/timeout(1))"
fi

finish
