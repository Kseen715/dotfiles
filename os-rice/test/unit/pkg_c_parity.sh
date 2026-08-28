#!/bin/sh
# Proves lib/pkg.c installs what lib/pkg.sh installed: the same commands, with
# the same arguments, in the same order.
#
# Hermetic the way test/unit/module_c_parity.sh is: PATH is reduced to a stub
# bin/, so "is dpkg installed", "is this package held" and every install
# command are properties of the scenario rather than of this machine. Each stub
# logs its argv, and that log IS the comparison -- what a package layer does to
# a box is exactly the list of commands it decided to run.
#
# Both sides go through their own resolver over the REAL lib/pkgmap/, so a map
# row that only one of them understands shows up here as a diff.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip pkg_c_parity: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
BIN="$TMP/bin"; mkdir -p "$BIN" "$TMP/home"
LOG="$TMP/log"; export LOG

# stub <name> [exit-code] -- a fake tool that records how it was called.
stub() {
    rm -f "$BIN/$1"
    cat >"$BIN/$1" <<EOF
#!/bin/sh
printf '%s %s\n' "$1" "\$*" >>"\$LOG"
exit ${2:-0}
EOF
    chmod +x "$BIN/$1"
}
# The tools the sh libs need to run at all. Real ones: what they answer is not
# part of what is being compared (both sides ask the same questions of them).
# `command -v` answers a BUILTIN with its bare name (test, printf, true), so a
# bare answer is looked up on disk instead: linking the bare name would make a
# dangling symlink, and every use of it would then fail as 127 on BOTH sides at
# once -- an agreement that proves nothing. `test` really is called as a
# program here, by as_user test -x.
for _t in sh env cat cut grep sed awk tr head tail printf id mktemp rm cp mkdir \
          tee sort od wc dirname basename sleep kill test true false; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in
        /*) ln -sf "$_p" "$BIN/$_t" ;;
        *)  for _d in /usr/bin /bin /usr/local/bin; do
                [ -x "$_d/$_t" ] && { ln -sf "$_d/$_t" "$BIN/$_t"; break; }
            done ;;
    esac
done

# sudo records the escalation and then runs the command, so `as_root apt-get`
# in sh and osr_run_root() in C leave the same two lines.
cat >"$BIN/sudo" <<'EOF'
#!/bin/sh
printf 'sudo %s\n' "$*" >>"$LOG"
[ "$1" = "-u" ] && shift 2
exec "$@"
EOF
chmod +x "$BIN/sudo"

FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB OSR_DISTRO=ubuntu OSR_ID_LIKE=debian
       OSR_CODENAME=noble OSR_VERSION_ID=24.04 OSR_ARCH=x86_64
       OSR_USER=tester OSR_HOME=$TMP/home NO_COLOR=1 TERM=dumb
       OSR_LOG=$TMP/run.log OSR_VERBOSE=1"

# run_sh <env-overrides> <names...> -- lib/pkg.sh's pkg_install.
run_sh() {
    _env=$1; shift
    : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" $FACTS $_env HOME="$TMP/home" sh -c '
        . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
        for l in detect user net pkg; do . "$OSR_LIB/$l.sh"; done
        pkg_install "$@"' _ "$@" >"$TMP/sh.out" 2>&1 || :
    cp "$LOG" "$TMP/sh.log"
}

# run_sh_remove / run_c_remove -- the same two sides for pkg_remove.
run_sh_remove() {
    _env=$1; shift
    : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" $FACTS $_env HOME="$TMP/home" sh -c '
        . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
        for l in detect user net pkg; do . "$OSR_LIB/$l.sh"; done
        pkg_remove "$@"' _ "$@" >"$TMP/sh.out" 2>&1 || :
    cp "$LOG" "$TMP/sh.log"
}

run_c_remove() {
    _env=$1; shift
    : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" $FACTS $_env HOME="$TMP/home" \
        "$OSR_BIN" pkg remove "$@" >"$TMP/c.out" 2>&1 || :
    cp "$LOG" "$TMP/c.log"
}

# run_c <env-overrides> <names...> -- lib/pkg.c, through the core.
run_c() {
    _env=$1; shift
    : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" $FACTS $_env HOME="$TMP/home" \
        "$OSR_BIN" pkg install "$@" >"$TMP/c.out" 2>&1 || :
    cp "$LOG" "$TMP/c.log"
}

# `id -u` is only how the SHELL as_root asks whether it is already root; the C
# tier calls getuid(). Same decision, one fewer process.
normalize() { grep -v '^id -u$' "$1" || :; }

# compare_quiet -- the same diff for scenarios whose CORRECT answer is "no
# command at all": compare()'s empty-log guard would read that as a broken
# sandbox.
compare_quiet() {
    if [ "$(normalize "$TMP/sh.log")" = "$(normalize "$TMP/c.log")" ]; then
        ok "$1"
    else
        fail "$1"
        diff -u "$TMP/sh.log" "$TMP/c.log" >&2 || :
    fi
}

compare() {
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

# --- 1. apt: nothing installed yet -------------------------------------------
APT="OSR_PKG=apt"
stub dpkg 1                 # nothing installed
stub apt-get 0
stub apt-mark 1             # nothing held
run_sh "$APT" zsh git curl; run_c "$APT" zsh git curl
compare "apt: three packages, one batched install"
assert_contains "$TMP/c.log" "apt-get install -y -q -o Dpkg::Use-Pty=0 zsh git curl" \
    "apt: one install command, not three"

# --- 2. apt: everything already there (the rerun) ----------------------------
stub dpkg 0
run_sh "$APT" zsh git curl; run_c "$APT" zsh git curl
compare "apt: a rerun installs nothing"
refute_contains "$TMP/c.log" "apt-get install" "apt: no install on a rerun"

# --- 2b. a held package (G2: never override user-defined package state) -----
stub dpkg 1
cat >"$BIN/apt-mark" <<'EOF'
#!/bin/sh
printf 'apt-mark %s\n' "$*" >>"$LOG"
echo vim
EOF
chmod +x "$BIN/apt-mark"
run_sh "$APT" zsh vim; run_c "$APT" zsh vim
compare "apt: a held package is skipped, the rest still installs"
refute_contains "$TMP/c.log" "apt-get install .*vim" "apt: the held package is not installed"
assert_contains "$TMP/c.log" "apt-get install -y -q -o Dpkg::Use-Pty=0 zsh" "apt: the unheld one still is"
stub apt-mark 1

# --- 3. a name the map rewrites ----------------------------------------------
# `fd = fd-find` on apt: the batch must carry the REAL name.
stub dpkg 1
run_sh "$APT" fd; run_c "$APT" fd
compare "apt: a mapped name installs its real package"
assert_contains "$TMP/c.log" "fd-find" "apt: fd resolved to fd-find"

# --- 4. one-to-many rows -----------------------------------------------------
# `build = build-essential ...`: one logical name, several real packages, still
# one install command.
stub dpkg 1
run_sh "$APT" build; run_c "$APT" build
compare "apt: a one-to-many row expands into the same batch"

# --- 5. pacman ---------------------------------------------------------------
PAC="OSR_PKG=pacman"
stub pacman 1               # -Q says "not installed"; -S then also "fails"
cat >"$BIN/pacman" <<'EOF'
#!/bin/sh
printf 'pacman %s\n' "$*" >>"$LOG"
[ "$1" = "-Q" ] && exit 1
exit 0
EOF
chmod +x "$BIN/pacman"
run_sh "$PAC" zsh fd; run_c "$PAC" zsh fd
compare "pacman: --needed --noconfirm, one call"

# --- 6. an unknown manager ---------------------------------------------------
# Neither side may invent an installer for a manager it does not know.
run_sh "OSR_PKG=nosuchmgr" zsh; run_c "OSR_PKG=nosuchmgr" zsh
if [ ! -s "$TMP/c.log" ]; then ok "unknown manager: C runs no install command"
else fail "unknown manager: C ran something"; fi

# --- 6b. the script: provider (curl | sh) ------------------------------------
# The map is the real one, so this needs a name that resolves to a script: row
# on some manager. OSR_PKGMAP_DIR is not a thing -- instead the scenario picks
# `starship`, which is script: everywhere it is not native.
SCRIPT_ENV="$APT"
stub dpkg 1
stub curl 0
stub apt-get 0
_m=$(env -i PATH="$BIN" $FACTS $SCRIPT_ENV "$OSR_BIN" pkg map starship)
case "$_m" in
    script:*)
        run_sh "$SCRIPT_ENV" starship; run_c "$SCRIPT_ENV" starship
        compare "script: fetched and piped into a shell the same way"
        assert_contains "$TMP/c.log" "sudo -u tester sh -s --" "script: the installer runs as the user"
        assert_contains "$TMP/c.log" "curl -fsSL" "script: fetched with curl"
        ;;
    *)  ok "script: skipped (starship is not a script: row on apt)" ;;
esac

# --- 6c. the cargo: provider -------------------------------------------------
# The probes are `as_user test -x ~/.cargo/bin/<x>`, so the scenario is built
# out of real files under a sandboxed OSR_HOME.
mkdir -p "$TMP/home/.cargo/bin"
printf '#!/bin/sh\nprintf "cargo %%s\\n" "$*" >>"$LOG"\n' >"$TMP/home/.cargo/bin/cargo"
chmod +x "$TMP/home/.cargo/bin/cargo"
_m=$(env -i PATH="$BIN" $FACTS $APT "$OSR_BIN" pkg map serie)
case "$_m" in
    cargo:*)
        run_sh "$APT" serie; run_c "$APT" serie
        compare "cargo: installs the crate as the user, --locked"
        assert_contains "$TMP/c.log" "cargo install --locked serie" "cargo: --locked, no binstall asset"
        ;;
    *)  ok "cargo: skipped (serie is native on apt)" ;;
esac

# cargo-binstall present, and no prebuilt asset for this crate: the binstall
# path is tried first and its failure falls back to the source build rather
# than ending the install. (The probe is the cargo-binstall FILE; the call is
# `cargo binstall`, so the failure has to come from the cargo stub.)
touch "$TMP/home/.cargo/bin/cargo-binstall"
chmod +x "$TMP/home/.cargo/bin/cargo-binstall"
cat >"$TMP/home/.cargo/bin/cargo" <<'EOF'
#!/bin/sh
printf 'cargo %s\n' "$*" >>"$LOG"
[ "$1" = binstall ] && exit 1
exit 0
EOF
chmod +x "$TMP/home/.cargo/bin/cargo"
case "$_m" in
    cargo:*)
        run_sh "$APT" serie; run_c "$APT" serie
        compare "cargo: binstall first, then the source build when it fails"
        assert_contains "$TMP/c.log" "binstall --no-confirm serie" "cargo: binstall was tried"
        assert_contains "$TMP/c.log" "cargo install --locked serie" "cargo: and the fallback ran"
        ;;
esac
rm -f "$TMP/home/.cargo/bin/cargo-binstall"

# --- 6d. the aur: provider ---------------------------------------------------
# aur: rows only exist on pacman, and the helper is resolved at install time.
cat >"$BIN/paru" <<'EOF'
#!/bin/sh
printf 'paru %s\n' "$*" >>"$LOG"
EOF
chmod +x "$BIN/paru"
_m=$(env -i PATH="$BIN" $FACTS $PAC "$OSR_BIN" pkg map wlogout)
case "$_m" in
    aur:*)
        run_sh "$PAC" wlogout; run_c "$PAC" wlogout
        compare "aur: installed through the helper, as the user"
        assert_contains "$TMP/c.log" "paru -S --needed --noconfirm" "aur: paru with the same flags"
        ;;
    *)  ok "aur: skipped (wlogout is not an aur: row here)" ;;
esac
rm -f "$BIN/paru"

# --- 7. pkg_installed --------------------------------------------------------
stub dpkg 0
_r=$(env -i PATH="$BIN" LOG="$LOG" $FACTS OSR_PKG=apt sh -c '
    . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/detect.sh"; . "$OSR_LIB/pkg.sh"
    pkg_installed zsh && echo yes || echo no')
_c=$(env -i PATH="$BIN" LOG="$LOG" $FACTS OSR_PKG=apt "$OSR_BIN" pkg installed zsh \
     >/dev/null 2>&1 && echo yes || echo no)
assert_eq "$_r" "$_c" "pkg_installed: agrees when installed"
stub dpkg 1
_r=$(env -i PATH="$BIN" LOG="$LOG" $FACTS OSR_PKG=apt sh -c '
    . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/detect.sh"; . "$OSR_LIB/pkg.sh"
    pkg_installed zsh && echo yes || echo no')
_c=$(env -i PATH="$BIN" LOG="$LOG" $FACTS OSR_PKG=apt "$OSR_BIN" pkg installed zsh \
     >/dev/null 2>&1 && echo yes || echo no)
assert_eq "$_r" "$_c" "pkg_installed: agrees when absent"

# --- 8. the index refresh ----------------------------------------------------
# pkg_refresh is not called on its own: it runs once, lazily, before the first
# install, so every scenario above already compared it. These pin the two
# branches with more in them than one command.

# 8a. apt: the bootstrap list is pruned once the vendor describes the same repo.
# The whole check is pointed at a sandboxed tree through
# OSR_APT_BOOTSTRAP_LISTS - the searched directories are derived from the
# list's own path, so /etc is never read here.
ETC="$TMP/etc/apt"; mkdir -p "$ETC/sources.list.d"
YB="$ETC/sources.list.d/yandex-browser.list"
: >"$ETC/sources.list"
printf 'deb [signed-by=/etc/apt/keyrings/yandex.asc] http://repo.yandex.ru/yandex-browser/deb stable main\n' >"$YB"
printf 'deb [signed-by=/usr/share/keyrings/yandex.gpg] http://repo.yandex.ru/yandex-browser/deb/ stable main\n' \
    >"$ETC/sources.list.d/yandex-browser-beta.list"
stub dpkg 1
stub apt-get 0
PRUNE="$APT OSR_APT_BOOTSTRAP_LISTS=$YB"
run_sh "$PRUNE" zsh
_sh_pruned=no; [ -f "$YB" ] || _sh_pruned=yes
printf 'deb [signed-by=/etc/apt/keyrings/yandex.asc] http://repo.yandex.ru/yandex-browser/deb stable main\n' >"$YB"
run_c "$PRUNE" zsh
_c_pruned=no; [ -f "$YB" ] || _c_pruned=yes
compare "apt refresh: the same commands when a bootstrap list is dropped"
assert_eq yes "$_sh_pruned" "apt refresh: sh drops the duplicated bootstrap list"
assert_eq yes "$_c_pruned" "apt refresh: C drops it too"
assert_contains "$TMP/c.log" "rm -f $YB" "apt refresh: dropped through as_root rm"

# 8b. the same tree, but nothing else describes that repo: the list stays.
printf 'deb [signed-by=/etc/apt/keyrings/yandex.asc] http://repo.yandex.ru/yandex-browser/deb stable main\n' >"$YB"
rm -f "$ETC/sources.list.d/yandex-browser-beta.list"
run_sh "$PRUNE" zsh
_sh_kept=no; [ -f "$YB" ] && _sh_kept=yes
run_c "$PRUNE" zsh
_c_kept=no; [ -f "$YB" ] && _c_kept=yes
compare "apt refresh: nothing dropped when only our list describes the repo"
assert_eq yes "$_sh_kept" "apt refresh: sh keeps the only list"
assert_eq yes "$_c_kept" "apt refresh: C keeps it too"
refute_contains "$TMP/c.log" "rm -f" "apt refresh: no removal without a duplicate"

# 8c. portage: getuto when it exists, then the webrsync snapshot.
POR="OSR_PKG=portage"
stub emerge 1               # -p --quiet says "not installed"
cat >"$BIN/emerge" <<'EOF'
#!/bin/sh
printf 'emerge %s\n' "$*" >>"$LOG"
case "$*" in *-p*) exit 1 ;; esac
exit 0
EOF
chmod +x "$BIN/emerge"
stub getuto 0
stub emerge-webrsync 0
run_sh "$POR" zsh; run_c "$POR" zsh
compare "portage refresh: getuto, then emerge-webrsync"
assert_contains "$TMP/c.log" "emerge-webrsync" "portage: the snapshot sync"

# 8d. portage without emerge-webrsync: the plain --sync fallback.
rm -f "$BIN/emerge-webrsync" "$BIN/getuto"
run_sh "$POR" zsh; run_c "$POR" zsh
compare "portage refresh: emerge --sync when webrsync is absent"
assert_contains "$TMP/c.log" "emerge --sync --quiet" "portage: the rsync fallback"
rm -f "$BIN/emerge"

# --- 9. xbps: a conflicting installed package -------------------------------
# The dry run is the authority on what conflicts. Its report is stderr in the
# real thing, so the stub writes there too: a port that captured only stdout
# would silently clear nothing.
XB="OSR_PKG=xbps"
stub xbps-query 1           # -Q form: nothing installed; -X form: no revdeps
cat >"$BIN/xbps-install" <<'EOF'
#!/bin/sh
printf 'xbps-install %s\n' "$*" >>"$LOG"
[ "$1" = "-n" ] || exit 0
echo 'CONFLICT: unclutter-xfixes-1.6_1 with installed pkg unclutter-8_5 (matched by unclutter>=0)' >&2
exit 1
EOF
chmod +x "$BIN/xbps-install"
cat >"$BIN/xbps-uhelper" <<'EOF'
#!/bin/sh
printf 'xbps-uhelper %s\n' "$*" >>"$LOG"
echo unclutter
EOF
chmod +x "$BIN/xbps-uhelper"
stub xbps-remove 0
run_sh "$XB" zsh; run_c "$XB" zsh
compare "xbps: the blocking package is removed before the batch"
assert_contains "$TMP/c.log" "xbps-remove -y unclutter" "xbps: the conflict was cleared"
assert_contains "$TMP/c.log" "xbps-install -y" "xbps: and the batch still ran"

# 9b. something else depends on it: G2 wins, it stays.
cat >"$BIN/xbps-query" <<'EOF'
#!/bin/sh
printf 'xbps-query %s\n' "$*" >>"$LOG"
[ "$1" = "-X" ] && { echo some-other-pkg-1_1; exit 0; }
exit 1
EOF
chmod +x "$BIN/xbps-query"
run_sh "$XB" zsh; run_c "$XB" zsh
compare "xbps: a package with reverse dependencies is left alone"
refute_contains "$TMP/c.log" "xbps-remove" "xbps: nothing removed when something needs it"
rm -f "$BIN/xbps-install" "$BIN/xbps-uhelper" "$BIN/xbps-remove" "$BIN/xbps-query"

# --- 10. pkg_remove ----------------------------------------------------------
stub dpkg 0                 # everything is installed
stub apt-get 0
run_sh_remove "$APT" zsh git; run_c_remove "$APT" zsh git
compare "remove: one apt-get remove for the installed ones"
assert_contains "$TMP/c.log" "apt-get remove -y zsh git" "remove: batched, -y"

# 10b. nothing installed: nothing is passed down, because every native remover
# errors on an unknown package and that would make a first run fatal (§2).
stub dpkg 1
run_sh_remove "$APT" zsh git; run_c_remove "$APT" zsh git
compare_quiet "remove: both sides stay quiet when nothing is installed"
refute_contains "$TMP/c.log" "apt-get remove" "remove: an absent package removes nothing"

# 10c. a provider row is not ours to remove.
stub dpkg 0
_m=$(env -i PATH="$BIN" $FACTS $APT "$OSR_BIN" pkg map starship)
case "$_m" in
    script:*)
        run_sh_remove "$APT" starship; run_c_remove "$APT" starship
        compare_quiet "remove: a script: row is skipped with a warning"
        refute_contains "$TMP/c.log" "apt-get remove" "remove: no native remove for a provider row"
        ;;
    *)  ok "remove: skipped (starship is native on apt here)" ;;
esac

# 10d. portage: deselect, then the depclean that actually removes them.
# `qlist -I -e` is how the portage branch answers "is it installed".
stub emerge 0
stub qlist 0
run_sh_remove "$POR" zsh; run_c_remove "$POR" zsh
compare "remove: portage deselects and then depcleans"
assert_contains "$TMP/c.log" "emerge --depclean --quiet" "remove: the depclean ran"
rm -f "$BIN/emerge" "$BIN/qlist"

finish
