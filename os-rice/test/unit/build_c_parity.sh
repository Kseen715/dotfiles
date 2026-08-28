#!/bin/sh
# Proves lib/build.c's source: builders do what lib/build.sh's did: the same
# release URL, the same extraction, the same install command.
#
# Hermetic like test/unit/pkg_c_parity.sh: PATH is reduced to a stub bin/, the
# stubs log their argv, and that log IS the comparison -- a builder is nothing
# but the list of commands it decided to run. curl answers the GitHub API with
# a fixed release, so the resolved version is a property of the scenario and
# not of the network.
#
# Two differences are normalized away, both "a shell had to fork for what libc
# does": the sh side calls `mktemp -d`, `find` and `head` where the C side uses
# mkdtemp() and a readdir walk, and the random temp suffix differs per run.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip build_c_parity: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
BIN="$TMP/bin"; mkdir -p "$BIN" "$TMP/home" "$TMP/scratch"
LOG="$TMP/log"; export LOG

stub() {
    rm -f "$BIN/$1"
    cat >"$BIN/$1" <<EOF
#!/bin/sh
printf '%s %s\n' "$1" "\$*" >>"\$LOG"
exit ${2:-0}
EOF
    chmod +x "$BIN/$1"
}

# The planting stubs need a REAL mkdir, but `mkdir` itself has to become a
# logging no-op: `as_root mkdir -p /usr/local/zig-<v>` is a builder's actual
# command, and running it for real would put a directory under /usr/local on a
# box that happens to run the suite as root. Resolve one now, before PATH goes.
REALMKDIR=$(command -v mkdir); export REALMKDIR
TMPROOT=$TMP; export TMPROOT

for _t in sh env cat cut grep sed awk tr head tail printf id mktemp rm cp \
          find sort wc dirname basename sleep kill test true false; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in
        /*) ln -sf "$_p" "$BIN/$_t" ;;
        *)  for _d in /usr/bin /bin /usr/local/bin; do
                [ -x "$_d/$_t" ] && { ln -sf "$_d/$_t" "$BIN/$_t"; break; }
            done ;;
    esac
done

cat >"$BIN/sudo" <<'EOF'
#!/bin/sh
printf 'sudo %s\n' "$*" >>"$LOG"
[ "$1" = "-u" ] && shift 2
exec "$@"
EOF
chmod +x "$BIN/sudo"

# curl -- the one stub with behaviour. It answers the GitHub API with a fixed
# release so the version is the scenario's, and it CREATES the -o destination,
# because everything after the download (tar, apt-get) only runs when the file
# is there.
cat >"$BIN/curl" <<'EOF'
#!/bin/sh
printf 'curl %s\n' "$*" >>"$LOG"
_dest=""; _prev=""; _url=""
for _a in "$@"; do
    [ "$_prev" = "-o" ] && _dest=$_a
    case "$_a" in https://*) _url=$_a ;; esac
    _prev=$_a
done
case "$_url" in
    *api.github.com*/releases/latest) _json='{"tag_name": "v1.2.3"}' ;;
    *api.github.com*)                 _json='[{"name": "v1.2.3"}]' ;;
    # The zig download index, with BOTH asset namings upstream has used --
    # zig-<arch>-linux on 0.15+, zig-linux-<arch> on <= 0.14 -- and newest
    # first, which is the order the "no version pinned" path relies on.
    *ziglang.org*index.json)
        _json='{
  "0.15.1": {
    "x86_64-linux": { "tarball": "https://ziglang.org/download/0.15.1/zig-x86_64-linux-0.15.1.tar.xz" },
    "aarch64-linux": { "tarball": "https://ziglang.org/download/0.15.1/zig-aarch64-linux-0.15.1.tar.xz" }
  },
  "0.14.1": {
    "x86_64-linux": { "tarball": "https://ziglang.org/download/0.14.1/zig-linux-x86_64-0.14.1.tar.xz" }
  }
}' ;;
    *)                                _json="" ;;
esac
if [ -n "$_dest" ]; then
    printf 'payload\n' >"$_dest"
elif [ -n "$_json" ]; then
    printf '%s\n' "$_json"
fi
exit 0
EOF
chmod +x "$BIN/curl"

# tar -- logs, then plants the binary the builder is about to look for, so both
# sides' "find it anywhere inside" runs against the same tree.
cat >"$BIN/tar" <<'EOF'
#!/bin/sh
printf 'tar %s\n' "$*" >>"$LOG"
_dir=""; _prev=""
for _a in "$@"; do [ "$_prev" = "-C" ] && _dir=$_a; _prev=$_a; done
[ -n "$_dir" ] || exit 0
# Only ever plant inside the sandbox: a builder that unpacks into a real prefix
# (zig -> /usr/local/zig-<v>) must not have that prefix created here.
case "$_dir" in "$TMPROOT"*) ;; *) exit 0 ;; esac
"$REALMKDIR" -p "$_dir/inner"
for _b in $PLANT; do
    printf '#!/bin/sh\n' >"$_dir/inner/$_b"
    chmod +x "$_dir/inner/$_b"
done
exit 0
EOF
chmod +x "$BIN/tar"

# unzip -- the .zip half of the same trick, for the builders that fetch an
# archive holding SEVERAL binaries (yazi ships `yazi` and `ya` in one .zip).
cat >"$BIN/unzip" <<'EOF'
#!/bin/sh
printf 'unzip %s\n' "$*" >>"$LOG"
_dir=""; _prev=""
for _a in "$@"; do [ "$_prev" = "-d" ] && _dir=$_a; _prev=$_a; done
[ -n "$_dir" ] || exit 0
case "$_dir" in "$TMPROOT"*) ;; *) exit 0 ;; esac
"$REALMKDIR" -p "$_dir/inner"
for _b in $PLANT; do
    printf '#!/bin/sh\n' >"$_dir/inner/$_b"
    chmod +x "$_dir/inner/$_b"
done
exit 0
EOF
chmod +x "$BIN/unzip"

stub install
stub apt-get
stub dpkg 1
# mkdir and ln are MUTATIONS a builder makes outside the sandbox (zig installs a
# whole tree at /usr/local/zig-<v> and symlinks it), so they log and do nothing.
stub mkdir
stub ln
stub dnf
stub rpm 1
# bash is the interpreter provide_ghostty_deb pipes the upstream installer into.
# A logging stub keeps that a command comparison and not a real install; it also
# satisfies the builder's `command -v bash` probe, so no package is resolved.
stub bash

FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB OSR_DISTRO=ubuntu OSR_ID_LIKE=debian
       OSR_CODENAME=noble OSR_VERSION_ID=24.04 OSR_ARCH=x86_64 OSR_ARCH_DEB=amd64
       OSR_USER=tester OSR_HOME=$TMP/home NO_COLOR=1 TERM=dumb COLUMNS=80
       TMPDIR=$TMP/scratch OSR_LOG=$TMP/run.log OSR_VERBOSE=1"

# run_sh <builder> [env] -- the shell function, with the libs it needs around it.
run_sh() {
    _fn=$1; _env=${2:-}
    : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" PLANT="$PLANT" REALMKDIR="$REALMKDIR" \
        TMPROOT="$TMPROOT" $FACTS $_env HOME="$TMP/home" sh -c '
        . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
        for l in detect user net pkg build; do . "$OSR_LIB/$l.sh"; done
        "$1"' _ "$_fn" >"$TMP/sh.out" 2>&1 || :
    cp "$LOG" "$TMP/sh.log"
}

# run_c <builder> [env] -- the same builder out of the registry in lib/build.c.
run_c() {
    _fn=$1; _env=${2:-}
    : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" PLANT="$PLANT" REALMKDIR="$REALMKDIR" \
        TMPROOT="$TMPROOT" $FACTS $_env HOME="$TMP/home" \
        "$OSR_BIN" build run "$_fn" >"$TMP/c.out" 2>&1 || :
    cp "$LOG" "$TMP/c.log"
}

# normalize -- drop the plumbing a shell forks for and libc does not (mktemp,
# find, head), and collapse the random mktemp suffix so two runs of the same
# scenario read the same.
normalize() {
    grep -Ev '^(mktemp|find|head|id) ' "$1" | sed 's#tmp\.[A-Za-z0-9]*#tmp.X#g'
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

# both <builder> <planted-binaries> [env] -- run the pair. The second argument
# is the space-separated list of binaries the archive stubs should plant, or
# any placeholder for a builder that unpacks nothing.
both() {
    PLANT=$2; export PLANT
    run_sh "$1" "${3:-}"
    run_c  "$1" "${3:-}"
}

# --- 1. the tarball builders -------------------------------------------------
both provide_gh_tarball gh
compare "provide_gh_tarball: same release URL, extraction and install"
assert_contains "$TMP/c.log" 'gh_1.2.3_linux_amd64.tar.gz' \
    "provide_gh_tarball: the asset carries the resolved version and the dpkg arch"
assert_contains "$TMP/c.log" 'install -m 0755 .* /usr/local/bin/gh' \
    "provide_gh_tarball: the binary lands in /usr/local/bin, mode 0755"
assert_contains "$TMP/c.log" 'sudo install -m 0755' \
    "provide_gh_tarball: and it escalates to put it there"

both provide_btop_tarball btop
compare "provide_btop_tarball: same release URL, extraction and install"
assert_contains "$TMP/c.log" 'btop-x86_64-unknown-linux-musl.tar.gz' \
    "provide_btop_tarball: the asset arch is uname-style"

both provide_lsd_tarball lsd
compare "provide_lsd_tarball: same release URL, extraction and install"
assert_contains "$TMP/c.log" 'lsd-v1.2.3-x86_64-unknown-linux-gnu.tar.gz' \
    "provide_lsd_tarball: the asset repeats the tag, v and all"

both provide_fastfetch_tarball fastfetch
compare "provide_fastfetch_tarball: same release URL, extraction and install"
assert_contains "$TMP/c.log" 'fastfetch-linux-amd64.tar.gz' \
    "provide_fastfetch_tarball: x86_64 asks for the amd64 asset"

# --- 2. arch handling --------------------------------------------------------
both provide_btop_tarball btop "OSR_ARCH=aarch64"
compare "provide_btop_tarball: aarch64 resolves to its own asset"
assert_contains "$TMP/c.log" 'btop-aarch64-unknown-linux-musl.tar.gz' \
    "provide_btop_tarball: aarch64 asset"

both provide_btop_tarball btop "OSR_ARCH=riscv64"
compare "provide_btop_tarball: an arch with no asset stops, on both sides"
refute_contains "$TMP/c.log" 'install -m 0755' \
    "provide_btop_tarball: and nothing is installed when it does"

# --- 3. fzf, whose idempotency is a VERSION, not presence --------------------
# No fzf on PATH: the release binary is fetched.
both provide_fzf fzf
compare "provide_fzf: with no fzf on PATH, the release binary is installed"
assert_contains "$TMP/c.log" 'fzf-1.2.3-linux_amd64.tar.gz' \
    "provide_fzf: the asset arch is Go's, not uname's"

# An fzf older than FZF_MIN is not good enough: still fetched.
cat >"$BIN/fzf" <<'EOF'
#!/bin/sh
printf '%s %s\n' fzf "$*" >>"$LOG"
printf '0.60 (devel)\n'
EOF
chmod +x "$BIN/fzf"
both provide_fzf fzf
compare "provide_fzf: an fzf older than the minimum is replaced"
assert_contains "$TMP/c.log" 'fzf-1.2.3-linux_amd64.tar.gz' \
    "provide_fzf: the old one does not satisfy the probe"

# A new enough fzf: nothing is downloaded, and both say so the same way.
cat >"$BIN/fzf" <<'EOF'
#!/bin/sh
printf '%s %s\n' fzf "$*" >>"$LOG"
printf '0.74.3 (15f64c49)\n'
EOF
chmod +x "$BIN/fzf"
both provide_fzf fzf
compare "provide_fzf: a new enough fzf is left alone"
refute_contains "$TMP/c.log" 'curl' \
    "provide_fzf: nothing is downloaded for an fzf that already works"
assert_contains "$TMP/c.out" 'fzf 0.74 is already >= 0.66' \
    "provide_fzf: and it says which version it found"
rm -f "$BIN/fzf"

# --- 4. the .deb builders ----------------------------------------------------
both provide_lsd_deb lsd
compare "provide_lsd_deb: same .deb URL and apt-get install"
assert_contains "$TMP/c.log" 'lsd_1.2.3_amd64.deb' \
    "provide_lsd_deb: the asset name drops the tag's v and carries the dpkg arch"
assert_contains "$TMP/c.log" 'apt-get install -y .*lsd_1.2.3_amd64.deb' \
    "provide_lsd_deb: apt-get installs the local file, so its deps come too"
assert_contains "$TMP/c.log" 'sudo env DEBIAN_FRONTEND=noninteractive apt-get' \
    "provide_lsd_deb: escalated and non-interactive"

both provide_fastfetch_deb fastfetch
compare "provide_fastfetch_deb: same .deb URL and apt-get install"
assert_contains "$TMP/c.log" 'fastfetch-linux-amd64.deb' \
    "provide_fastfetch_deb: x86_64 asks for the amd64 .deb"

both provide_fastfetch_deb fastfetch "OSR_ARCH=riscv64 OSR_ARCH_DEB=riscv64"
compare "provide_fastfetch_deb: an unlisted arch falls back to the dpkg name"
assert_contains "$TMP/c.log" 'fastfetch-linux-riscv64.deb' \
    "provide_fastfetch_deb: the fallback asset is the dpkg-named one"

# --- 5. yazi: a .zip holding two binaries, with a cargo fallback -------------
both provide_yazi_bin "yazi ya"
compare "provide_yazi_bin: same release .zip, extraction and both installs"
assert_contains "$TMP/c.log" 'yazi-x86_64-unknown-linux-gnu.zip' \
    "provide_yazi_bin: the asset is the glibc target for this arch"
assert_contains "$TMP/c.log" 'install -m 0755 .* /usr/local/bin/yazi' \
    "provide_yazi_bin: the TUI lands in /usr/local/bin"
assert_contains "$TMP/c.log" 'install -m 0755 .* /usr/local/bin/ya' \
    "provide_yazi_bin: and so does the ya CLI, out of the same archive"

# An arch with no release asset never touches the network: it goes to cargo,
# which then reports the missing toolchain rather than installing anything.
both provide_yazi_bin "yazi ya" "OSR_ARCH=riscv64"
compare "provide_yazi_bin: an arch with no asset falls back to cargo on both sides"
refute_contains "$TMP/c.log" 'unzip' \
    "provide_yazi_bin: nothing is extracted on the fallback path"
assert_contains "$TMP/c.out" 'falling back to cargo' \
    "provide_yazi_bin: and it says why"

# --- 6. zig: a whole TREE, resolved out of index.json ------------------------
both provide_zig zig "OSR_PKG=apt"
compare "provide_zig: same tarball URL, unpack prefix and symlink"
assert_contains "$TMP/c.log" 'zig-x86_64-linux-0.15.1.tar.xz' \
    "provide_zig: no pin takes the first entry in the index, which is the newest"
assert_contains "$TMP/c.log" 'sudo tar -xf .* -C /usr/local/zig-0.15.1 --strip-components=1' \
    "provide_zig: it unpacks as a tree under a versioned prefix, not a bare binary"
assert_contains "$TMP/c.log" 'sudo ln -sf /usr/local/zig-0.15.1/zig /usr/local/bin/zig' \
    "provide_zig: and /usr/local/bin/zig is the symlink into it"

both provide_zig zig "OSR_PKG=apt ZIG_VERSION=0.14.1"
compare "provide_zig: a pinned version picks that release on both sides"
assert_contains "$TMP/c.log" 'zig-linux-x86_64-0.14.1.tar.xz' \
    "provide_zig: and matches the OLD asset naming that release used"
assert_contains "$TMP/c.log" 'sudo mkdir -p /usr/local/zig-0.14.1' \
    "provide_zig: the prefix carries the pinned version"

# The arch is judged before anything is fetched, so BOTH logs are empty here --
# which is the assertion, and is the one thing compare() cannot make.
both provide_zig zig "OSR_PKG=apt OSR_ARCH=riscv64"
if [ ! -s "$TMP/sh.log" ] && [ ! -s "$TMP/c.log" ]; then
    ok "provide_zig: an arch with no tarball stops before running anything, on both sides"
else
    fail "provide_zig: an arch with no tarball stops before running anything, on both sides"
    diff -u "$TMP/sh.log" "$TMP/c.log" >&2 || :
fi
assert_contains "$TMP/c.out" 'no zig tarball for arch riscv64' \
    "provide_zig: and it names the arch it cannot serve"

# --- 7. ghostty: the two prebuilt community routes ---------------------------
both provide_ghostty_copr none "OSR_PKG=dnf"
compare "provide_ghostty_copr: same COPR enable and dnf install"
assert_contains "$TMP/c.log" 'sudo dnf copr enable -y scottames/ghostty' \
    "provide_ghostty_copr: the COPR is enabled before the install"
assert_contains "$TMP/c.log" 'sudo dnf install -y ghostty' \
    "provide_ghostty_copr: and ghostty then comes from it"

both provide_ghostty_deb none "OSR_PKG=apt"
compare "provide_ghostty_deb: same installer fetched into the same interpreter"
assert_contains "$TMP/c.log" 'curl .*mkasberg/ghostty-ubuntu/HEAD/install.sh' \
    "provide_ghostty_deb: the upstream installer is streamed, not saved"
assert_contains "$TMP/c.log" 'sudo bash' \
    "provide_ghostty_deb: and it runs as root, which is what it needs"

# --- 8. the row that names the builder ---------------------------------------
# `fzf@noble = source:provide_fzf` in apt.map, driven through the whole package
# layer, is the wiring lib/pkg.c's M_SOURCE case exists for.
PLANT=fzf; export PLANT
: >"$LOG"
# shellcheck disable=SC2086
env -i PATH="$BIN" LOG="$LOG" PLANT=fzf REALMKDIR="$REALMKDIR" TMPROOT="$TMPROOT" \
    $FACTS OSR_PKG=apt HOME="$TMP/home" sh -c '
    . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
    for l in detect user net pkg build; do . "$OSR_LIB/$l.sh"; done
    pkg_install fzf' _ >"$TMP/sh.out" 2>&1 || :
cp "$LOG" "$TMP/sh.log"
: >"$LOG"
# shellcheck disable=SC2086
env -i PATH="$BIN" LOG="$LOG" PLANT=fzf REALMKDIR="$REALMKDIR" TMPROOT="$TMPROOT" \
    $FACTS OSR_PKG=apt HOME="$TMP/home" \
    "$OSR_BIN" pkg install fzf >"$TMP/c.out" 2>&1 || :
cp "$LOG" "$TMP/c.log"
compare "pkg_install fzf: the source: row reaches the same builder from both tiers"

# --- 9. the registry is what lib/pkg.c dispatches on -------------------------
if "$OSR_BIN" build has provide_fzf; then
    ok "osr build has: a ported builder is claimed by the C tier"
else
    fail "osr build has: a ported builder is claimed by the C tier"
fi
if "$OSR_BIN" build has provide_wezterm; then
    fail "osr build has: an unported builder still belongs to lib/build.sh"
else
    ok "osr build has: an unported builder still belongs to lib/build.sh"
fi

finish
