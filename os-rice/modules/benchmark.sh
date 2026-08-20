# session: x11+wayland
# modules/benchmark.sh — the tools `osr benchmark cpu` needs.
#
# Deliberately small. The benchmark reads power, temperature and clocks straight
# out of sysfs (/sys/class/powercap, /sys/class/hwmon, cpufreq) rather than
# shelling out to turbostat or sensors, so the only hard requirement is the
# workload itself. That keeps this module installable on a machine where the
# kernel-tools package does not exist — which is most non-x86 ones.
#
# stress-ng is the workload: packaged everywhere, and the only common stressor
# with a --verify mode, which is what the undervolt stability ladder needs later
# (modules/undervolt.sh builds on this module rather than repeating it).
#
# lm_sensors is a convenience for the human, not a dependency: `sensors` is how
# you sanity-check that the temperature the benchmark reports is the one you
# think it is. kernel-tools (turbostat/perf) likewise — useful for cross-checking
# a RAPL figure by hand. Both are best-effort: a missing one warns, it does not
# fail the module, because neither changes what the benchmark can measure.

run_step "Installing benchmark workload" pkg_install stress-ng

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

# RAPL is the most accurate power source and is root-only on most kernels since
# the PLATYPUS side channel (CVE-2020-8694). Say so once, here, rather than
# leaving the user to wonder why `osr benchmark cpu` reports no power.
if [ -d /sys/class/powercap ]; then
    if [ -r /sys/class/powercap/intel-rapl:0/energy_uj ]; then
        info "RAPL energy counter is readable - power measurement will work"
    else
        info 'RAPL present but root-only - run osr benchmark cpu with sudo for power numbers'
    fi
else
    info "no RAPL on this machine - benchmark will fall back to hwmon or battery"
fi
