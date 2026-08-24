# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/benchmark.sh — everything `osr benchmark cpu` needs to produce a
# number, including a power and temperature reading.
#
# The workload is the easy half. stress-ng is packaged everywhere and is the
# only common stressor with a --verify mode, which is what the undervolt
# stability ladder needs later (modules/undervolt.sh builds on this module
# rather than repeating it).
#
# The sensors are the hard half, and what is usually missing is a DRIVER rather
# than hardware: `intel_rapl_msr` is what registers the powercap tree that RAPL
# is read from, and k10temp/coretemp are what publish the package temperature.
# On a machine where nothing has loaded them, /sys/class/powercap is an empty
# directory and the benchmark has nothing to read. This module loads them and
# keeps them loaded.
#
# Some machines genuinely have no readable sensor -- a VM, a container, a board
# whose super-I/O chip nothing supports. `osr benchmark sensors` says which
# case a given machine is in.
#
# Everything here is best-effort. A missing sensor degrades the report to
# throughput-only; it must never fail the module, because the throughput
# numbers are useful on their own and are the reason most people run this.

run_step "Installing benchmark workload" pkg_install stress-ng

# --- the drivers the kernel needs to expose the sensors ----------------------

# osr_bench_load_sensor_modules — the drivers that make the sensors visible.
#
# This is the single most common reason a bare-metal machine reports no power.
# RAPL is not a file the kernel always publishes: `intel_rapl_msr` (with
# `intel_rapl_common` underneath it) is what registers the powercap tree, and on
# a machine where nothing has asked for it that tree is an empty directory. The
# name is historical -- since 5.11 the same driver serves AMD's Zen parts too,
# which is why it is loaded regardless of vendor.
#
# The temperature drivers are the same story: k10temp/coretemp are the
# difference between "peak temp 89 C" and the field being absent. Usually
# autoloaded, but not in a container, not in a VM, and not on a kernel booted
# with a trimmed module set.
#
# Every one is best-effort. A built-in shows up as an immediate success, an
# absent one as a note, and neither fails the module.
osr_bench_load_sensor_modules() {
    # Order matters only in that intel_rapl_common is a dependency; modprobe
    # pulls it in either way, and naming it makes the intent legible.
    _bs_want="msr intel_rapl_common intel_rapl_msr"
    case "${OSR_CPU_VENDOR:-}" in
        *AMD*|*amd*)     _bs_want="$_bs_want k10temp" ;;
        *Intel*|*intel*) _bs_want="$_bs_want coretemp" ;;
        *)               _bs_want="$_bs_want k10temp coretemp" ;;
    esac
    for _bs_m in $_bs_want; do
        # Already built in, or already loaded: modprobe says nothing and exits
        # 0, so there is no need to check first.
        as_root modprobe "$_bs_m" 2>/dev/null \
            || info "kernel module $_bs_m not available - not required"
    done
}

# osr_bench_persist_sensor_modules — make the load survive a reboot.
#
# Without this the benchmark works today and silently loses its power reading
# after the next boot, which is a worse failure than never having had one: the
# numbers stop being comparable and nothing says why. /etc/modules-load.d is
# the systemd interface and is read by every distro that has systemd; on the
# rest the modprobe above still has to be repeated, which the module does on
# every run anyway.
osr_bench_persist_sensor_modules() {
    [ -d /etc/modules-load.d ] || return 0
    _bp_tmp="${TMPDIR:-/tmp}/osr-bench-modules-$$"
    {
        printf '# Written by os-rice (modules/benchmark.sh).\n'
        printf '# The powercap RAPL driver: without it /sys/class/powercap is empty\n'
        printf '# and osr benchmark cpu has no power source to read.\n'
        printf 'intel_rapl_msr\n'
    } >"$_bp_tmp"
    install_layer "$_bp_tmp" /etc/modules-load.d/osr-benchmark.conf
    rm -f "$_bp_tmp"
}

# osr_bench_have_hwmon — is there anything at all under /sys/class/hwmon?
osr_bench_have_hwmon() {
    # An `if`, not `[ ... ] && return`: under `set -e` a failing AND-OR list is
    # itself a failing command, so the short form would end the run on the
    # first glob that did not match.
    for _bhh_d in /sys/class/hwmon/hwmon*; do
        if [ -e "$_bhh_d" ]; then return 0; fi
    done
    return 1
}

# --- run ----------------------------------------------------------------------

# A guest with no passthrough has no sensor to find, and no driver load or
# probe will change that. Said once, plainly, instead of leaving three steps to
# fail in a row and look like breakage.
case "${OSR_VIRT:-none}" in
    wsl|docker|podman|lxc|lxc-libvirt|systemd-nspawn)
        info "${OSR_VIRT} guest: power and temperature sensors are not reachable from in here"
        info "throughput still measures normally - see: osr benchmark sensors"
        ;;
esac

# try_step, not a bare pkg_install: an unwrapped install streams the package
# manager's whole transcript -- several hundred lines of Get:/Unpacking/Setting
# up -- straight into the run, burying the two lines that are actually about the
# benchmark. try_step puts it in the same greyed live window every other step
# uses and collapses it to one line, while still letting a failure be survivable
# (both of these are cross-check conveniences, not requirements).
try_step "Installing sensors cross-check" pkg_install lm_sensors \
    || warn 'lm_sensors not installed - the sensors(1) cross-check is unavailable'

# turbostat/perf live in a package whose name varies more than most, and on
# several distros it is tied to the running kernel version. Not worth failing
# over: nothing in osr calls them.
try_step "Installing kernel power tools" pkg_install kernel-tools \
    || info "kernel-tools not available here - not required"

run_step "Loading CPU sensor drivers" osr_bench_load_sensor_modules
try_step "Keeping them loaded across reboots" osr_bench_persist_sensor_modules \
    || info "could not write /etc/modules-load.d - the drivers load per run instead"

# sensors-detect is what turns a board's super-I/O chip into hwmon nodes,
# and on a desktop that chip is often the only thing measuring anything.
# --auto answers every prompt with the safe default; it is still a probe of
# unknown I/O ports, so it only runs when the tree is otherwise EMPTY --
# where there is nothing to find by gentler means and nothing to lose.
if command -v sensors-detect >/dev/null 2>&1 && ! osr_bench_have_hwmon; then
    try_step "Probing for board sensors (sensors-detect --auto)" \
        as_root sensors-detect --auto \
        || info "sensors-detect found nothing - this board may have no readable sensors"
fi

# RAPL is the most accurate power source and is root-only on most kernels
# since the PLATYPUS side channel (CVE-2020-8694). Say so once, here, rather
# than leaving the user to wonder why `osr benchmark cpu` reports no power.
if [ -d /sys/class/powercap ] && [ -n "$(ls -A /sys/class/powercap 2>/dev/null)" ]; then
    if [ -r /sys/class/powercap/intel-rapl:0/energy_uj ]; then
        info "RAPL energy counter is readable - power measurement will work"
    else
        info 'RAPL present but root-only - run osr benchmark cpu with sudo for power numbers'
    fi
else
    info "no RAPL on this machine - the benchmark will fall back to hwmon or battery"
fi
