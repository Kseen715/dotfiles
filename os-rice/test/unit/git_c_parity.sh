#!/bin/sh
# Proves lib/git.c does to a repo exactly what lib/git.sh did: the same git
# commands in the same order, the same messages, the same tree left behind,
# and the same patched oh-my-zsh installer fed to sh.
#
# Hermetic like test/unit/net_c_parity.sh: PATH is reduced to a stub bin/, so
# git, sudo, curl and even sh are scenario-controlled and nothing here clones
# from the network. Each scenario runs twice against twin sandboxes seeded
# identically, and both the command log and the resulting tree are compared
# byte for byte.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip git_c_parity: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
BIN="$TMP/bin"; mkdir -p "$BIN"
REAL_SH=$(command -v sh)

for _t in env cat cut grep sed awk tr head tail printf id mktemp rm cp mv mkdir \
          rmdir ls find sort od wc dirname basename chown sleep kill stat \
          touch test true false; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in
        /*) ln -sf "$_p" "$BIN/$_t" ;;
        *)  for _d in /usr/bin /bin /usr/local/bin; do
                [ -x "$_d/$_t" ] && { ln -sf "$_d/$_t" "$BIN/$_t"; break; }
            done ;;
    esac
done

# --- the stubs ----------------------------------------------------------------
# git: every invocation is logged, and the few subcommands this file drives are
# simulated off marker files, so a scenario is a directory layout rather than a
# real clone.
cat >"$BIN/git" <<'EOF'
#!/bin/sh
printf 'git %s\n' "$*" >>"$LOG"
_dir=""
if [ "$1" = "-C" ]; then _dir=$2; shift 2; fi
case "$1 ${2:-}" in
    "remote get-url")
        [ -f "$_dir/.git/REMOTE" ] || exit 1
        cat "$_dir/.git/REMOTE" ;;
    "diff --quiet")   [ -f "$_dir/.git/DIRTY" ] && exit 1 ; exit 0 ;;
    "diff --cached")  [ -f "$_dir/.git/STAGED" ] && exit 1 ; exit 0 ;;
    "pull --ff-only") [ -f "$_dir/.git/PULLFAIL" ] && exit 3 ; echo "Already up to date." ;;
    "reset --hard")   rm -f "$_dir/.git/DIRTY" "$_dir/.git/STAGED" ;;
    "clean -fd")      rm -f "$_dir/dirt" ;;
    clone*)
        shift
        while [ $# -gt 2 ]; do shift; done
        mkdir -p "$2/.git"
        printf '%s\n' "$1" >"$2/.git/REMOTE"
        printf 'core\n' >"$2/oh-my-zsh.sh" ;;
esac
exit 0
EOF

# sudo: logged, then the real command, so as_root's escalation is visible in
# the log without the test needing any privilege.
cat >"$BIN/sudo" <<'EOF'
#!/bin/sh
printf 'sudo %s\n' "$*" >>"$LOG"
if [ "$1" = "-u" ]; then shift 2; fi
exec "$@"
EOF

# sh: `sh -s` is the oh-my-zsh installer being fed on stdin. Logging that
# stdin is the only way to see the sed patch both tiers apply to it; every
# other use of sh runs for real.
cat >"$BIN/sh" <<EOF
#!/bin/sh
if [ "\$1" = "-s" ]; then
    shift
    { printf 'sh -s args=[%s]\n' "\$*"; sed 's/^/  | /'; } >>"\$LOG"
    exit 0
fi
exec $REAL_SH "\$@"
EOF

# curl: the omz installer payload, from a file this test wrote.
cat >"$BIN/curl" <<'EOF'
#!/bin/sh
printf 'curl %s\n' "$*" >>"$LOG"
cat "$PAYLOAD"
EOF
chmod +x "$BIN/git" "$BIN/sudo" "$BIN/sh" "$BIN/curl"

cat >"$TMP/omz-install.sh" <<'EOF'
#!/bin/sh
setup_shell() {
  chsh -s /bin/zsh "$USER" || exit 1
}
main() {
  setup_shell
  exec env zsh -l
}
main "$@"
EOF
PAYLOAD="$TMP/omz-install.sh"; export PAYLOAD

FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB OSR_DISTRO=ubuntu OSR_ID_LIKE=debian
       OSR_ARCH=x86_64 NO_COLOR=1 TERM=dumb OSR_PKG=apt PAYLOAD=$PAYLOAD"
ME=$(id -un)

# --- the harness --------------------------------------------------------------
# seed <root> -- redefined per scenario; lays out one sandbox. $ROOT is bound.
seed() { :; }

# run_side <root> <sh|c> <command> -- run one tier against its own sandbox.
# The sh side evaluates the snippet with lib/git.sh in scope; the C side runs
# the same thing through `osr git`.
run_side() {
    _root=$1; _tier=$2; _cmd=$3
    rm -rf "$_root"; mkdir -p "$_root/home"
    ROOT=$_root; seed
    : >"$_root/log"
    if [ "$_tier" = sh ]; then
        # shellcheck disable=SC2086
        env -i PATH="$BIN" LOG="$_root/log" $FACTS OSR_USER="$ME" \
            OSR_HOME="$_root/home" HOME="$_root/home" ROOT="$_root" \
            "$BIN/sh" -c '
                . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"
                . "$OSR_LIB/net.sh"; . "$OSR_LIB/git.sh"
                eval "$1"' _ "$_cmd" 2>&1 || :
    else
        # shellcheck disable=SC2086
        env -i PATH="$BIN" LOG="$_root/log" $FACTS OSR_USER="$ME" \
            OSR_HOME="$_root/home" HOME="$_root/home" ROOT="$_root" \
            "$BIN/sh" -c 'eval "$1"' _ "$_cmd" 2>&1 || :
    fi
}

# dump_tree <root> -- every path under the sandbox plus its contents, with the
# sandbox path itself collapsed so two roots compare equal.
dump_tree() {
    (cd "$1" && find . -path ./log -prune -o -print | sort | while read -r _p; do
        printf '%s\n' "$_p"
        [ -f "$_p" ] && [ ! -L "$_p" ] && sed 's/^/    /' <"$_p"
    done) | sed "s|$1|ROOT|g"
}

# scene <label> <sh-snippet> <c-args> -- run both tiers and compare everything
# that is observable: messages, git command log, resulting tree.
scene() {
    _label=$1
    _sh_out=$(run_side "$TMP/a" sh "$2")
    _c_out=$(run_side "$TMP/b" c "\"$OSR_BIN\" git $3")
    assert_eq "$_sh_out" "$_c_out" "$_label: same output"
    assert_eq "$(sed "s|$TMP/a|ROOT|g" <"$TMP/a/log")" \
              "$(sed "s|$TMP/b|ROOT|g" <"$TMP/b/log")" "$_label: same commands"
    assert_eq "$(dump_tree "$TMP/a")" "$(dump_tree "$TMP/b")" "$_label: same tree"
}

URL=https://github.com/zsh-users/zsh-autosuggestions

# --- 1. a repo that is not there yet ------------------------------------------
seed() { :; }
scene "fresh clone" \
    'install_or_update_git_repo demo '"$URL"' "$ROOT/home/demo" --depth 1' \
    "repo demo $URL \"\$ROOT/home/demo\" --depth 1"

# --- 2. a repo already there, same remote, clean ------------------------------
seed() {
    mkdir -p "$ROOT/home/demo/.git"
    printf '%s\n' "$URL" >"$ROOT/home/demo/.git/REMOTE"
}
scene "clean repo pulls" \
    'install_or_update_git_repo demo '"$URL"' "$ROOT/home/demo" --depth 1' \
    "repo demo $URL \"\$ROOT/home/demo\" --depth 1"

# The three spellings of the same remote all count as a match.
seed() {
    mkdir -p "$ROOT/home/demo/.git"
    printf '%s.git\n' "$URL" >"$ROOT/home/demo/.git/REMOTE"
}
scene "remote with .git matches" \
    'install_or_update_git_repo demo '"$URL"' "$ROOT/home/demo"' \
    "repo demo $URL \"\$ROOT/home/demo\""

seed() {
    mkdir -p "$ROOT/home/demo/.git"
    printf '%s\n' "$URL" >"$ROOT/home/demo/.git/REMOTE"
}
scene "url with .git matches a bare remote" \
    'install_or_update_git_repo demo '"$URL"'.git "$ROOT/home/demo"' \
    "repo demo $URL.git \"\$ROOT/home/demo\""

# --- 3. a dirty tree is reset before the pull ---------------------------------
seed() {
    mkdir -p "$ROOT/home/demo/.git"
    printf '%s\n' "$URL" >"$ROOT/home/demo/.git/REMOTE"
    : >"$ROOT/home/demo/.git/DIRTY"
    printf 'junk\n' >"$ROOT/home/demo/dirt"
}
scene "dirty tree reset then pulled" \
    'install_or_update_git_repo demo '"$URL"' "$ROOT/home/demo"' \
    "repo demo $URL \"\$ROOT/home/demo\""

# A staged-only change counts too: sh probed the index separately.
seed() {
    mkdir -p "$ROOT/home/demo/.git"
    printf '%s\n' "$URL" >"$ROOT/home/demo/.git/REMOTE"
    : >"$ROOT/home/demo/.git/STAGED"
}
scene "staged change reset then pulled" \
    'install_or_update_git_repo demo '"$URL"' "$ROOT/home/demo"' \
    "repo demo $URL \"\$ROOT/home/demo\""

# --- 4. a different remote is thrown away and recloned ------------------------
seed() {
    mkdir -p "$ROOT/home/demo/.git"
    printf 'https://example.invalid/other\n' >"$ROOT/home/demo/.git/REMOTE"
    printf 'stale\n' >"$ROOT/home/demo/stale-file"
}
scene "different remote recloned" \
    'install_or_update_git_repo demo '"$URL"' "$ROOT/home/demo" --depth 1' \
    "repo demo $URL \"\$ROOT/home/demo\" --depth 1"

# A repo directory with no .git at all is just a clone target.
seed() { mkdir -p "$ROOT/home/demo"; printf 'x\n' >"$ROOT/home/demo/loose"; }
scene "directory without .git is cloned into" \
    'install_or_update_git_repo demo '"$URL"' "$ROOT/home/demo"' \
    "repo demo $URL \"\$ROOT/home/demo\""

# --- 5. a failing pull is fatal, with check_error's wording -------------------
seed() {
    mkdir -p "$ROOT/home/demo/.git"
    printf '%s\n' "$URL" >"$ROOT/home/demo/.git/REMOTE"
    : >"$ROOT/home/demo/.git/PULLFAIL"
}
scene "failed pull is fatal" \
    'install_or_update_git_repo demo '"$URL"' "$ROOT/home/demo"' \
    "repo demo $URL \"\$ROOT/home/demo\""

# --- 6. the oh-my-zsh plugin path --------------------------------------------
seed() { :; }
scene "plugin lands in custom/plugins" \
    'install_zsh_plugin zsh-autosuggestions '"$URL" \
    "plugin zsh-autosuggestions $URL"

seed() {
    mkdir -p "$ROOT/home/.oh-my-zsh/custom/plugins/zsh-autosuggestions/.git"
    printf '%s\n' "$URL" \
        >"$ROOT/home/.oh-my-zsh/custom/plugins/zsh-autosuggestions/.git/REMOTE"
}
scene "existing plugin is updated" \
    'install_zsh_plugin zsh-autosuggestions '"$URL" \
    "plugin zsh-autosuggestions $URL"

# --- 7. install_omz: the three states ----------------------------------------
# Already installed: the probe is the FILE.
seed() {
    mkdir -p "$ROOT/home/.oh-my-zsh"
    printf 'core\n' >"$ROOT/home/.oh-my-zsh/oh-my-zsh.sh"
}
scene "omz already installed" 'install_omz' 'omz'

# The stub case: a directory with no core in it. The clone lands and the
# existing custom/ is carried across rather than re-cloned.
seed() {
    mkdir -p "$ROOT/home/.oh-my-zsh/custom/plugins/zsh-autosuggestions"
    printf 'plugin\n' \
        >"$ROOT/home/.oh-my-zsh/custom/plugins/zsh-autosuggestions/file.zsh"
}
scene "omz stub directory is filled in" 'install_omz' 'omz'

# A stub with no custom/ at all: nothing to carry, just the swap.
seed() { mkdir -p "$ROOT/home/.oh-my-zsh/cache"; }
scene "omz stub without custom" 'install_omz' 'omz'

# Nothing there: fetch upstream's installer, patch it, feed it to sh.
seed() { :; }
scene "omz installed from upstream" 'install_omz' 'omz'

finish
