#!/bin/sh
# Proves modules/swap.c sizes zram and disk swap from the machine it runs on:
# the RAM tiers, an existing swap partition counted in full, the free-disk cap,
# the idempotent skips, and the container no-op. Hermetic: fixture files for
# /proc/{meminfo,swaps} + fstab, every mutating command mocked, no root.
#
# The module is C now, so the plan is read where the module publishes it -- the
# one summary line it prints -- rather than out of shell variables it no longer
# has. Everything else is observed the way every C-tier test observes: PATH is
# reduced to a stub bin/, `sudo` logs every escalation and executes only the
# harmless writes, and the fixture files are pointed at by the module's own
# OSR_* knobs.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip swap_sizing: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
OUT="$TMP/out"
BIN="$TMP/bin"; mkdir -p "$BIN" "$TMP/home"
MEMINFO="$TMP/meminfo"; SWAPS="$TMP/swaps"; FSTAB="$TMP/fstab"
ZRAM_CONF="$TMP/zram.conf"; SYSCTL_CONF="$TMP/sysctl.conf"
SWAPFILE="$TMP/swapfile"; ZRAMEN_CONF="$TMP/zramen.conf"

for _t in sh env cat grep sed printf id rm mkdir mktemp test true false tee \
          cp chmod cut tr head sort wc dirname basename; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$BIN/$_t" ;; esac
done

# sudo logs every escalation and executes only the file writes - the real
# mkswap/swapon/systemctl must never run against the machine running the tests.
cat >"$BIN/sudo" <<'EOF'
#!/bin/sh
printf 'ROOT %s\n' "$*" >>"$OUT"
case "$1" in tee|mkdir|rm|cp) exec "$@" ;; esac
exit 0
EOF
# df reports whatever free space the fixture asked for.
cat >"$BIN/df" <<'EOF'
#!/bin/sh
printf 'Filesystem 1024-blocks Used Available Capacity Mounted\n/dev/sda1 %s %s %s 50%% /\n' \
    "$((FREE_MIB * 2048))" "$((FREE_MIB * 1024))" "$((FREE_MIB * 1024))"
EOF
# The package manager: `-Q` says nothing is installed, so every install is
# attempted and shows up in the escalation log above (sudo logs it and stops
# there - pacman itself is never run).
cat >"$BIN/pacman" <<'EOF'
#!/bin/sh
[ "$1" = "-Q" ] && exit 1
exit 0
EOF
chmod +x "$BIN/sudo" "$BIN/df" "$BIN/pacman"

# fixture <ram-mib> <free-mib> [swaps-lines...]
fixture() {
    printf 'MemTotal:       %s kB\n' "$(($1 * 1024))" >"$MEMINFO"
    FREE_MIB=$2; shift 2
    printf 'Filename\t\t\t\tType\t\tSize\t\tUsed\t\tPriority\n' >"$SWAPS"
    for _l in "$@"; do printf '%s\n' "$_l" >>"$SWAPS"; done
    : >"$FSTAB"; rm -f "$ZRAM_CONF" "$SYSCTL_CONF" "$ZRAMEN_CONF" "$SWAPFILE"
    : >"$OUT"
}

# run <init> [virt] — the module, everything it can reach stubbed.
run() {
    env -i PATH="$BIN" OUT="$OUT" FREE_MIB="$FREE_MIB" \
        OSR_ROOT="$OSR_ROOT" OSR_LIB="$OSR_LIB" OSR_PKG=pacman \
        OSR_ARCH=x86_64 OSR_DISTRO=arch OSR_INIT="$1" OSR_VIRT="${2:-none}" \
        OSR_USER=tester OSR_HOME="$TMP/home" HOME="$TMP/home" \
        NO_COLOR=1 TERM=dumb OSR_VERBOSE=1 \
        OSR_MEMINFO="$MEMINFO" OSR_PROC_SWAPS="$SWAPS" OSR_FSTAB="$FSTAB" \
        OSR_ZRAM_CONF="$ZRAM_CONF" OSR_SYSCTL_CONF="$SYSCTL_CONF" \
        OSR_SWAPFILE="$SWAPFILE" OSR_ZRAMEN_CONF="$ZRAMEN_CONF" \
        "$OSR_BIN" module run swap >>"$OUT" 2>&1 || :
}

# The plan is read out of the one line the module prints it on:
#   swap: ram=8192M | zram want=8192M have=0M | disk want=8192M
#         have partition=0M file=0M free=200000M -> swapfile 8192M
plan_line() { grep -o 'swap: ram=.*swapfile [0-9]*M' "$OUT" | head -n 1; }
f_ram()       { plan_line | sed -n 's/.*ram=\([0-9]*\)M.*/\1/p'; }
f_zram_want() { plan_line | sed -n 's/.*zram want=\([0-9]*\)M.*/\1/p'; }
f_have_zram() { plan_line | sed -n 's/.*zram want=[0-9]*M have=\([0-9]*\)M.*/\1/p'; }
f_disk_want() { plan_line | sed -n 's/.*disk want=\([0-9]*\)M.*/\1/p'; }
f_have_part() { plan_line | sed -n 's/.*have partition=\([0-9]*\)M.*/\1/p'; }
f_have_file() { plan_line | sed -n 's/.*have partition=[0-9]*M file=\([0-9]*\)M.*/\1/p'; }
f_file_want() { plan_line | sed -n 's/.*swapfile \([0-9]*\)M.*/\1/p'; }

# plan — an init with no zram setter on purpose: it keeps the sizing fixtures
# from touching a real zram setter. It also means zram is UNREACHABLE in these
# cases, so swappiness here is the no-zram value - the reachable-init cases are
# asserted separately below, per init.
plan() { run none; }

# --- RAM tiers ---------------------------------------------------------------
fixture 8192 200000
plan
assert_eq 8192 "$(f_zram_want)" "8G RAM: zram covers RAM in full"
assert_eq 8192 "$(f_disk_want)" "8G RAM: disk target is RAM (hibernation fits)"
assert_contains "$OUT" 'no zram setter for init=none' \
    "init with no zram setter: zram not active despite the want"
assert_contains "$SYSCTL_CONF" 'vm.swappiness = 10' \
    "wanted zram but init cannot provide it: low swappiness"

fixture 4096 200000
plan
assert_eq 4096 "$(f_zram_want)" "4G RAM: zram covers RAM in full"

fixture 16384 200000
plan
assert_eq 8192  "$(f_zram_want)" "16G RAM: zram is min(RAM/2, 8G)"
assert_eq 16384 "$(f_disk_want)" "16G RAM: disk target is RAM"

# at/above the threshold zram is off entirely, so suspend-to-disk stays clean
fixture 24576 400000
plan
assert_eq 0     "$(f_zram_want)" "24G RAM: no zram"
assert_eq 16384 "$(f_disk_want)" "24G RAM: past the hibernation ceiling, overflow cap"
assert_contains "$SYSCTL_CONF" 'vm.swappiness = 10' "no zram: swappiness 10"

fixture 32768 400000
plan
assert_eq 0     "$(f_zram_want)" "32G RAM: no zram"
assert_eq 16384 "$(f_disk_want)" "32G RAM: overflow cap, not RAM-sized"

# past the hibernation ceiling: overflow swap only, never RAM-sized
fixture 65536 900000
plan
assert_eq 0     "$(f_zram_want)" "64G RAM: no zram"
assert_eq 16384 "$(f_disk_want)" "64G RAM: overflow cap, not RAM-sized"

fixture 262144 2000000
plan
assert_eq 0     "$(f_zram_want)" "256G RAM: no zram"
assert_eq 16384 "$(f_disk_want)" "256G RAM: still the 16G cap, not 256G of swap"
assert_contains "$OUT" 'no hibernation' "reports hibernation is off the table"

# MemTotal is always under the installed size (firmware/kernel reserve), so the
# tiers key off the rounded-up value - a "24G" box must not sneak back into zram.
fixture 23400 400000
plan
assert_eq 0     "$(f_zram_want)" "24G stick reporting 23.4G: still no zram"
assert_eq 16384 "$(f_disk_want)" "24G stick reporting 23.4G: overflow cap"

fixture 15600 400000
plan
assert_eq 8192  "$(f_zram_want)" "16G stick reporting 15.6G: zram is min(RAM/2, 8G)"
assert_eq 16384 "$(f_disk_want)" "16G stick reporting 15.6G: full 16G, image fits"

# --- existing swap partition is used in full ---------------------------------
fixture 16384 200000 '/dev/sda2                               partition	4194304	0	-2'
plan
assert_eq 4096  "$(f_have_part)" "swap partition detected"
assert_eq 12288 "$(f_file_want)" "swapfile covers only the remainder (16G - 4G)"

fixture 16384 200000 '/dev/sda2                               partition	20971520	0	-2'
plan
assert_eq 0 "$(f_file_want)" "partition bigger than the target: no swapfile"
assert_contains "$OUT" 'no swapfile needed' "reports the partition covers it"

# --- zram is not counted as disk swap ----------------------------------------
fixture 16384 200000 '/dev/zram0                              partition	8388608	0	100'
plan
assert_eq 8192 "$(f_have_zram)" "zram counted as zram"
assert_eq 0    "$(f_have_part)" "zram not counted as a swap partition"
assert_eq 16384 "$(f_file_want)" "zram does not reduce the disk target"

# --- free disk space caps the swapfile ---------------------------------------
fixture 32768 7000
plan
assert_eq 3072 "$(f_file_want)" "capped to half the free space, floored to GiB"
assert_contains "$OUT" 'capping' "warns when the disk caps the size"

fixture 32768 1000
plan
assert_eq 0 "$(f_file_want)" "under 1G spendable: no swapfile at all"

# the absolute reserve bites before the ratio does on a small disk
fixture 2048 4096
plan
assert_eq 2048 "$(f_file_want)" "2G RAM / 4G free: 2G swapfile, 2G left"
fixture 2048 3000
plan
assert_eq 0 "$(f_file_want)" "2G RAM / 3G free: nothing left to spare, no swapfile"

# the space of the file being replaced counts as free
fixture 32768 100 "$SWAPFILE	file	8388608	0	-2"
plan
assert_eq 4096 "$(f_file_want)" "existing swapfile's blocks count toward free space"

# --- actions: create + fstab + swappiness, and the skips on a second run -----
fixture 16384 200000
run systemd
assert_contains "$OUT" 'pacman -S .*zram-generator' "installs zram-generator on systemd"
assert_contains "$OUT" 'ROOT systemctl restart systemd-zram-setup@zram0' "restarts zram"
assert_contains "$ZRAM_CONF" 'zram-size = 8192' "zram config carries the computed size"
assert_contains "$OUT" "Creating $SWAPFILE (16384M)" "creates the swapfile"
assert_contains "$FSTAB" "$SWAPFILE none swap defaults,pri=10" "fstab entry keeps the swapfile below zram"
assert_contains "$SYSCTL_CONF" 'vm.swappiness = 100' "writes the zram swappiness"
assert_contains "$SYSCTL_CONF" 'vm.page-cluster = 0' "zram gets page-cluster 0"
assert_contains "$OUT" 'hibernation fits' "16G RAM + 16G disk swap: hibernation fits"
assert_contains "$ZRAM_CONF" 'swap-priority = 100' "zram outranks the swapfile"

# --- runit: zram via zramen, not zram-generator ------------------------------
# zram is a kernel feature, so an init without systemd is not an init without
# zram. Void ships zramen as a runit service; the policy must come out the same.
fixture 8192 200000
: >"$OUT"
run runit
assert_contains "$SYSCTL_CONF" 'vm.swappiness = 100' \
    "runit: zram is reachable, so it gets the zram swappiness"
assert_contains "$OUT" 'Installing zramen' "runit: installs zramen, not zram-generator"
assert_contains "$ZRAMEN_CONF" 'ZRAM_MAX_SIZE=8192' "runit: zramen carries the computed ceiling"
assert_contains "$ZRAMEN_CONF" 'ZRAM_SIZE=100' "8G RAM: zramen covers RAM in full"
assert_contains "$ZRAMEN_CONF" 'ZRAM_PRIORITY=100' "runit: zram outranks the swapfile"
assert_contains "$SYSCTL_CONF" 'vm.page-cluster = 0' "runit: zram gets page-cluster 0"

# The percentage has to track the tier, not just say 100.
fixture 16384 200000
: >"$OUT"
run runit
assert_contains "$ZRAMEN_CONF" 'ZRAM_SIZE=50' "16G RAM: zramen asks for half of RAM"
assert_contains "$ZRAMEN_CONF" 'ZRAM_MAX_SIZE=8192' "16G RAM: capped at the 8G ceiling"

# --- back to systemd for the idempotence pass --------------------------------
fixture 16384 200000
: >"$OUT"
run systemd

# second run: zram active + config unchanged, swapfile already the right size
printf '%s\tfile\t16777216\t0\t-2\n' "$SWAPFILE" >>"$SWAPS"
printf '/dev/zram0\tpartition\t8388608\t0\t100\n' >>"$SWAPS"
: >"$OUT"
run systemd
assert_contains "$OUT" 'zram already active' "second run skips the zram restart"
refute_contains "$OUT" 'Creating ' "second run skips the swapfile"
assert_contains "$OUT" 'vm.swappiness already 100' "second run skips the sysctl write"
assert_eq 1 "$(grep -c "^$SWAPFILE none swap" "$FSTAB")" "fstab entry not duplicated"

# --- big-RAM box: zram is not installed, and an old zram setup is torn down ---
fixture 32768 400000 '/dev/zram0                              partition\t8388608\t0\t100'
printf '[zram0]\nzram-size = 8192\n' >"$ZRAM_CONF"
run systemd
refute_contains "$OUT" 'Installing zram-generator' "32G RAM: zram-generator is not installed"
assert_contains "$OUT" 'Disabling zram' "32G RAM: an existing zram setup is removed"
assert_contains "$OUT" 'ROOT swapoff /dev/zram0' "32G RAM: zram device is swapped off"
assert_contains "$SYSCTL_CONF" 'vm.swappiness = 10' "no zram: low swappiness"
refute_contains "$SYSCTL_CONF" 'page-cluster' "no zram: page-cluster left alone"

# --- containers own no memory ------------------------------------------------
fixture 16384 200000
run systemd docker
refute_contains "$OUT" 'Installing' "docker guest: module is a no-op"

finish
