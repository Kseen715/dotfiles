#!/bin/sh
# Proves the pipewire/pulseaudio modules are mirror images (each removes the
# other stack before installing its own), that both no-op off pacman, and that
# pkg_remove filters packages that are not installed (a first run must not be
# fatal).
#
# Both modules are C now (modules/pipewire.c, modules/pulseaudio.c), so the
# assertions are made the way every C-tier test makes them: PATH is reduced to a
# stub bin/, the stubs log their argv, and that log is what the module decided
# to do. Their parity with the frozen sh originals is a separate question, and
# test/unit/module_c_parity.sh is where it is asked.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip audio_switch: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
BIN="$TMP/bin"; mkdir -p "$BIN" "$TMP/home"
LOG="$TMP/log"; export LOG

for _t in sh env cat grep sed printf id rm mkdir test true false; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$BIN/$_t" ;; esac
done
cat >"$BIN/sudo" <<'EOF'
#!/bin/sh
printf 'sudo %s\n' "$*" >>"$LOG"
[ "$1" = "-u" ] && shift 2
exec "$@"
EOF
chmod +x "$BIN/sudo"

# pacman -- the one stub with behaviour: `-Q <pkg>` is the "is it installed"
# probe, and which packages answer yes is the scenario. Everything else logs.
cat >"$BIN/pacman" <<'EOF'
#!/bin/sh
printf 'pacman %s\n' "$*" >>"$LOG"
if [ "$1" = "-Q" ] || [ "$1" = "-Qq" ]; then
    case " $INSTALLED " in *" $2 "*) exit 0 ;; *) exit 1 ;; esac
fi
exit 0
EOF
chmod +x "$BIN/pacman"

FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB OSR_DISTRO=arch OSR_ARCH=x86_64
       OSR_USER=tester OSR_HOME=$TMP/home NO_COLOR=1 TERM=dumb COLUMNS=80
       OSR_LOG=$TMP/run.log OSR_VERBOSE=1 OSR_INIT=systemd"

# run <module> [env] -- the C module with everything it can reach stubbed.
run() {
    : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" INSTALLED="${INSTALLED:-}" $FACTS ${2:-} \
        HOME="$TMP/home" "$OSR_BIN" module run "$1" >"$TMP/out" 2>&1 || :
}

# --- pacman: each module removes its rival, then installs itself --------------
# The box has the OTHER stack and not its own, which is the situation the swap
# exists for: the rival is there to be removed, and nothing the module installs
# drops out of the batch as already present.
INSTALLED="pulseaudio pulseaudio-ctl pulseaudio-equalizer pulseaudio-jack
pulseaudio-lirc pulseaudio-rtp jack2 jack2-dbus"
export INSTALLED
run pipewire "OSR_PKG=pacman"
assert_contains "$LOG" 'pacman -R --noconfirm .*pulseaudio.*jack2' \
    "pipewire removes PulseAudio/JACK"
assert_contains "$LOG" 'pacman -S --needed --noconfirm .*pipewire-pulse.*wireplumber' \
    "pipewire installs its stack"

INSTALLED="pipewire-pulse pipewire-jack pipewire-alsa pipewire-audio wireplumber pipewire"
export INSTALLED
run pulseaudio "OSR_PKG=pacman"
assert_contains "$LOG" 'pacman -R --noconfirm .*pipewire-pulse.*wireplumber' \
    "pulseaudio removes the PipeWire shims"
assert_contains "$LOG" 'pacman -S --needed --noconfirm .*pulseaudio.*jack2' \
    "pulseaudio installs its stack"
# The core `pipewire` package must NOT be in the removal list: the portals need
# it, and taking it out with the shims is what breaks screen sharing.
refute_contains "$LOG" 'pacman -R --noconfirm.* pipewire ' \
    "pipewire core kept (xdg-desktop-portal-wlr depends on it)"
refute_contains "$LOG" 'pacman -R --noconfirm.* pipewire$' \
    "pipewire core kept, last in the list too"

# --- non-pacman: both are no-ops ---------------------------------------------
run pipewire "OSR_PKG=apt"
assert_eq 0 "$(wc -l <"$LOG" | tr -d ' ')" "pipewire no-ops off pacman"
run pulseaudio "OSR_PKG=apt"
assert_eq 0 "$(wc -l <"$LOG" | tr -d ' ')" "pulseaudio no-ops off pacman"

# --- pkg_remove drops packages that are not installed ------------------------
# A first run has none of the rival stack: the removal must narrow to what is
# actually there rather than handing pacman a name it will refuse.
INSTALLED="pulseaudio"
export INSTALLED
: >"$LOG"
# shellcheck disable=SC2086
env -i PATH="$BIN" LOG="$LOG" INSTALLED="$INSTALLED" $FACTS OSR_PKG=pacman \
    HOME="$TMP/home" "$OSR_BIN" pkg remove pulseaudio jack2-dbus >/dev/null 2>&1 || :
assert_contains "$LOG" 'pacman -R --noconfirm pulseaudio$' \
    "removes only what is installed"

finish
