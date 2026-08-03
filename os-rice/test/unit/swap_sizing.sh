#!/bin/sh
# Proves modules/swap.sh sizes zram and disk swap from the machine it runs on:
# the RAM tiers, an existing swap partition counted in full, the free-disk cap,
# the idempotent skips, and the container no-op. Hermetic: fixture files for
# /proc/{meminfo,swaps} + fstab, every mutating command mocked, no root.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/out"
export OSR_MEMINFO="$TMP/meminfo" OSR_PROC_SWAPS="$TMP/swaps" \
       OSR_FSTAB="$TMP/fstab" OSR_ZRAM_CONF="$TMP/zram.conf" \
       OSR_SYSCTL_CONF="$TMP/sysctl.conf" OSR_SWAPFILE="$TMP/swapfile"

# --- mocks: nothing here may touch the real machine --------------------------
run_step()    { _d=$1; shift; echo "STEP $_d" >>"$OUT"; "$@"; }
pkg_install() { echo "INSTALL $*" >>"$OUT"; }
# as_root logs every escalation but executes nothing except `tee` - the real
# mkswap/swapon/systemctl must never run against the machine running the tests.
as_root() {
    echo "ROOT $*" >>"$OUT"
    if [ "$1" = tee ]; then shift; tee "$@" >/dev/null; fi
}
df() { printf 'Filesystem 1024-blocks Used Available Capacity Mounted\n/dev/sda1 %s %s %s 50%% /\n' \
           "$((FREE_MIB * 2048))" "$((FREE_MIB * 1024))" "$((FREE_MIB * 1024))"; }

# fixture <ram-mib> <free-mib> [swaps-lines...]
fixture() {
    printf 'MemTotal:       %s kB\n' "$(($1 * 1024))" >"$OSR_MEMINFO"
    FREE_MIB=$2; shift 2
    printf 'Filename\t\t\t\tType\t\tSize\t\tUsed\t\tPriority\n' >"$OSR_PROC_SWAPS"
    for _l in "$@"; do printf '%s\n' "$_l" >>"$OSR_PROC_SWAPS"; done
    : >"$OSR_FSTAB"; rm -f "$OSR_ZRAM_CONF" "$OSR_SYSCTL_CONF" "$OUT"; : >"$OUT"
}

# plan_only sources the module for its functions, then re-plans without acting.
plan() { OSR_INIT=none OSR_VIRT=none . "$OSR_ROOT/modules/swap.sh" >>"$OUT" 2>&1; }

# --- RAM tiers ---------------------------------------------------------------
fixture 8192 200000
plan
assert_eq 8192 "$SWAP_ZRAM_WANT" "8G RAM: zram covers RAM in full"
assert_eq 8192 "$SWAP_DISK_WANT" "8G RAM: disk target is RAM (hibernation fits)"
assert_eq 100  "$SWAP_SWAPPINESS" "zram present: swappiness 100"

fixture 4096 200000
plan
assert_eq 4096 "$SWAP_ZRAM_WANT" "4G RAM: zram covers RAM in full"

fixture 16384 200000
plan
assert_eq 8192  "$SWAP_ZRAM_WANT" "16G RAM: zram is min(RAM/2, 8G)"
assert_eq 16384 "$SWAP_DISK_WANT" "16G RAM: disk target is RAM"

# at/above the threshold zram is off entirely, so suspend-to-disk stays clean
fixture 24576 400000
plan
assert_eq 0     "$SWAP_ZRAM_WANT" "24G RAM: no zram"
assert_eq 16384 "$SWAP_DISK_WANT" "24G RAM: past the hibernation ceiling, overflow cap"
assert_eq 10    "$SWAP_SWAPPINESS" "no zram: swappiness 10"

fixture 32768 400000
plan
assert_eq 0     "$SWAP_ZRAM_WANT" "32G RAM: no zram"
assert_eq 16384 "$SWAP_DISK_WANT" "32G RAM: overflow cap, not RAM-sized"

# past the hibernation ceiling: overflow swap only, never RAM-sized
fixture 65536 900000
plan
assert_eq 0     "$SWAP_ZRAM_WANT" "64G RAM: no zram"
assert_eq 16384 "$SWAP_DISK_WANT" "64G RAM: overflow cap, not RAM-sized"

fixture 262144 2000000
plan
assert_eq 0     "$SWAP_ZRAM_WANT" "256G RAM: no zram"
assert_eq 16384 "$SWAP_DISK_WANT" "256G RAM: still the 16G cap, not 256G of swap"
assert_contains "$OUT" 'no hibernation' "reports hibernation is off the table"

# MemTotal is always under the installed size (firmware/kernel reserve), so the
# tiers key off the rounded-up value - a "24G" box must not sneak back into zram.
fixture 23400 400000
plan
assert_eq 0     "$SWAP_ZRAM_WANT" "24G stick reporting 23.4G: still no zram"
assert_eq 16384 "$SWAP_DISK_WANT" "24G stick reporting 23.4G: overflow cap"

fixture 15600 400000
plan
assert_eq 8192  "$SWAP_ZRAM_WANT" "16G stick reporting 15.6G: zram is min(RAM/2, 8G)"
assert_eq 16384 "$SWAP_DISK_WANT" "16G stick reporting 15.6G: full 16G, image fits"

# --- existing swap partition is used in full ---------------------------------
fixture 16384 200000 '/dev/sda2                               partition	4194304	0	-2'
plan
assert_eq 4096  "$SWAP_HAVE_PART" "swap partition detected"
assert_eq 12288 "$SWAP_FILE_WANT" "swapfile covers only the remainder (16G - 4G)"

fixture 16384 200000 '/dev/sda2                               partition	20971520	0	-2'
plan
assert_eq 0 "$SWAP_FILE_WANT" "partition bigger than the target: no swapfile"
assert_contains "$OUT" 'no swapfile needed' "reports the partition covers it"

# --- zram is not counted as disk swap ----------------------------------------
fixture 16384 200000 '/dev/zram0                              partition	8388608	0	100'
plan
assert_eq 8192 "$SWAP_HAVE_ZRAM" "zram counted as zram"
assert_eq 0    "$SWAP_HAVE_PART" "zram not counted as a swap partition"
assert_eq 16384 "$SWAP_FILE_WANT" "zram does not reduce the disk target"

# --- free disk space caps the swapfile ---------------------------------------
fixture 32768 7000
plan
assert_eq 3072 "$SWAP_FILE_WANT" "capped to half the free space, floored to GiB"
assert_contains "$OUT" 'capping' "warns when the disk caps the size"

fixture 32768 1000
plan
assert_eq 0 "$SWAP_FILE_WANT" "under 1G spendable: no swapfile at all"

# the absolute reserve bites before the ratio does on a small disk
fixture 2048 4096
plan
assert_eq 2048 "$SWAP_FILE_WANT" "2G RAM / 4G free: 2G swapfile, 2G left"
fixture 2048 3000
plan
assert_eq 0 "$SWAP_FILE_WANT" "2G RAM / 3G free: nothing left to spare, no swapfile"

# the space of the file being replaced counts as free
fixture 32768 100 "$OSR_SWAPFILE	file	8388608	0	-2"
plan
assert_eq 4096 "$SWAP_FILE_WANT" "existing swapfile's blocks count toward free space"

# --- actions: create + fstab + swappiness, and the skips on a second run -----
fixture 16384 200000
OSR_INIT=systemd OSR_VIRT=none . "$OSR_ROOT/modules/swap.sh" >>"$OUT" 2>&1
assert_contains "$OUT" 'INSTALL zram-generator' "installs zram-generator on systemd"
assert_contains "$OUT" 'ROOT systemctl restart systemd-zram-setup@zram0' "restarts zram"
assert_contains "$OSR_ZRAM_CONF" 'zram-size = 8192' "zram config carries the computed size"
assert_contains "$OUT" "STEP Creating $OSR_SWAPFILE (16384M)" "creates the swapfile"
assert_contains "$OSR_FSTAB" "$OSR_SWAPFILE none swap defaults,pri=10" "fstab entry keeps the swapfile below zram"
assert_contains "$OSR_SYSCTL_CONF" 'vm.swappiness = 100' "writes the zram swappiness"
assert_contains "$OSR_SYSCTL_CONF" 'vm.page-cluster = 0' "zram gets page-cluster 0"
assert_contains "$OUT" 'hibernation fits' "16G RAM + 16G disk swap: hibernation fits"
assert_contains "$OSR_ZRAM_CONF" 'swap-priority = 100' "zram outranks the swapfile"

# second run: zram active + config unchanged, swapfile already the right size
printf '%s\tfile\t16777216\t0\t-2\n' "$OSR_SWAPFILE" >>"$OSR_PROC_SWAPS"
printf '/dev/zram0\tpartition\t8388608\t0\t100\n' >>"$OSR_PROC_SWAPS"
: >"$OUT"
OSR_INIT=systemd OSR_VIRT=none . "$OSR_ROOT/modules/swap.sh" >>"$OUT" 2>&1
assert_contains "$OUT" 'zram already active' "second run skips the zram restart"
refute_contains "$OUT" 'STEP Creating' "second run skips the swapfile"
assert_contains "$OUT" 'vm.swappiness already 100' "second run skips the sysctl write"
assert_eq 1 "$(grep -c "^$OSR_SWAPFILE none swap" "$OSR_FSTAB")" "fstab entry not duplicated"

# --- big-RAM box: zram is not installed, and an old zram setup is torn down ---
fixture 32768 400000 '/dev/zram0                              partition\t8388608\t0\t100'
printf '[zram0]\nzram-size = 8192\n' >"$OSR_ZRAM_CONF"
OSR_INIT=systemd OSR_VIRT=none . "$OSR_ROOT/modules/swap.sh" >>"$OUT" 2>&1
refute_contains "$OUT" 'INSTALL zram-generator' "32G RAM: zram-generator is not installed"
assert_contains "$OUT" 'STEP Disabling zram' "32G RAM: an existing zram setup is removed"
assert_contains "$OUT" 'ROOT swapoff /dev/zram0' "32G RAM: zram device is swapped off"
assert_contains "$OSR_SYSCTL_CONF" 'vm.swappiness = 10' "no zram: low swappiness"
refute_contains "$OSR_SYSCTL_CONF" 'page-cluster' "no zram: page-cluster left alone"

# --- containers own no memory ------------------------------------------------
fixture 16384 200000
OSR_INIT=systemd OSR_VIRT=docker . "$OSR_ROOT/modules/swap.sh" >>"$OUT" 2>&1
refute_contains "$OUT" 'INSTALL' "docker guest: module is a no-op"

finish
