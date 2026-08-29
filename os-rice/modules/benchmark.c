/* modules/benchmark.c -- everything `osr benchmark cpu` needs to produce a
 * number, including a power and temperature reading.
 *
 * The workload is the easy half. stress-ng is packaged everywhere and is the
 * only common stressor with a --verify mode, which is what the undervolt
 * stability ladder needs later (lib/undervolt.c builds on this module rather
 * than repeating it).
 *
 * The sensors are the hard half, and what is usually missing is a DRIVER rather
 * than hardware: `intel_rapl_msr` is what registers the powercap tree that RAPL
 * is read from, and k10temp/coretemp are what publish the package temperature.
 * On a machine where nothing has loaded them, /sys/class/powercap is an empty
 * directory and the benchmark has nothing to read. This module loads them and
 * keeps them loaded.
 *
 * Some machines genuinely have no readable sensor -- a VM, a container, a board
 * whose super-I/O chip nothing supports. `osr benchmark sensors` says which case
 * a given machine is in.
 *
 * Everything here is best-effort. A missing sensor degrades the report to
 * throughput-only; it must never fail the module, because the throughput numbers
 * are useful on their own and are the reason most people run this.
 *
 * Port of modules/benchmark.sh, kept as the reference at
 * test/ref/benchmark_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <glob.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

/* The /etc/modules-load.d drop-in, verbatim -- see persist_sensor_modules. */
static const char *const MODULES_LOAD_CONF =
    "# Written by os-rice (modules/benchmark.sh).\n"
    "# The powercap RAPL driver: without it /sys/class/powercap is empty\n"
    "# and osr benchmark cpu has no power source to read.\n"
    "intel_rapl_msr\n";

/* load_sensor_modules -- the drivers that make the sensors visible.
 *
 * This is the single most common reason a bare-metal machine reports no power.
 * RAPL is not a file the kernel always publishes: `intel_rapl_msr` (with
 * `intel_rapl_common` underneath it) is what registers the powercap tree, and on
 * a machine where nothing has asked for it that tree is an empty directory. The
 * name is historical -- since 5.11 the same driver serves AMD's Zen parts too,
 * which is why it is loaded regardless of vendor.
 *
 * The temperature drivers are the same story: k10temp/coretemp are the
 * difference between "peak temp 89 C" and the field being absent. Usually
 * autoloaded, but not in a container, not in a VM, and not on a kernel booted
 * with a trimmed module set.
 *
 * Every one is best-effort. A built-in shows up as an immediate success, an
 * absent one as a note, and neither fails the module. */
static int load_sensor_modules(void *ctx) {
    /* Order matters only in that intel_rapl_common is a dependency; modprobe
     * pulls it in either way, and naming it makes the intent legible. */
    static const char *const common[] = { "msr", "intel_rapl_common", "intel_rapl_msr", NULL };
    const char *vendor = env_str("OSR_CPU_VENDOR", "");
    const char *extra[3];
    size_t i, n = 0;

    (void)ctx;
    if (strstr(vendor, "AMD") != NULL || strstr(vendor, "amd") != NULL) {
        extra[n++] = "k10temp";
    } else if (strstr(vendor, "Intel") != NULL || strstr(vendor, "intel") != NULL) {
        extra[n++] = "coretemp";
    } else {
        extra[n++] = "k10temp";
        extra[n++] = "coretemp";
    }
    extra[n] = NULL;

    for (i = 0; common[i] != NULL; i++) {
        char *argv[3];
        argv[0] = (char *)"modprobe"; argv[1] = (char *)common[i]; argv[2] = NULL;
        /* Already built in, or already loaded: modprobe says nothing and exits
         * 0, so there is no need to check first. */
        if (osr_run_root_quiet(argv) != 0)
            osr_infof("kernel module %s not available - not required", common[i]);
    }
    for (i = 0; extra[i] != NULL; i++) {
        char *argv[3];
        argv[0] = (char *)"modprobe"; argv[1] = (char *)extra[i]; argv[2] = NULL;
        if (osr_run_root_quiet(argv) != 0)
            osr_infof("kernel module %s not available - not required", extra[i]);
    }
    return 1;
}

/* persist_sensor_modules -- make the load survive a reboot.
 *
 * Without this the benchmark works today and silently loses its power reading
 * after the next boot, which is a worse failure than never having had one: the
 * numbers stop being comparable and nothing says why. /etc/modules-load.d is the
 * systemd interface and is read by every distro that has systemd; on the rest
 * the modprobe above still has to be repeated, which the module does on every
 * run anyway. */
static int persist_sensor_modules(void *ctx) {
    Str tmp;
    FILE *f;
    int ok = 1;

    (void)ctx;
    if (!dir_exists("/etc/modules-load.d")) return 1;

    str_init(&tmp);
    str_addz(&tmp, env_str("TMPDIR", "/tmp"));
    str_addz(&tmp, "/osr-bench-modules-");
    str_addl(&tmp, (long)getpid());
    f = fopen(str_text(&tmp), "wb");
    if (f == NULL) { str_free(&tmp); return 0; }
    fputs(MODULES_LOAD_CONF, f);
    fclose(f);
    ok = osr_install_layer(str_text(&tmp), "/etc/modules-load.d/osr-benchmark.conf");
    (void)unlink(str_text(&tmp));
    str_free(&tmp);
    return ok;
}

/* have_hwmon -- is there anything at all under /sys/class/hwmon? */
static int have_hwmon(void) {
    glob_t g;
    int found = 0;
    if (glob("/sys/class/hwmon/hwmon*", 0, NULL, &g) == 0) found = g.gl_pathc > 0;
    globfree(&g);
    return found;
}

/* powercap_populated -- `[ -d /sys/class/powercap ] && [ -n "$(ls -A ...)" ]`. */
static int powercap_populated(void) {
    glob_t g;
    int found = 0;
    if (!dir_exists("/sys/class/powercap")) return 0;
    if (glob("/sys/class/powercap/*", 0, NULL, &g) == 0) found = g.gl_pathc > 0;
    globfree(&g);
    return found;
}

static int sensors_detect(void *ctx) {
    char *argv[3];
    (void)ctx;
    argv[0] = (char *)"sensors-detect"; argv[1] = (char *)"--auto"; argv[2] = NULL;
    return osr_run_root(argv) == 0;
}

int osrm_benchmark(void) {
    static const char *const workload[] = { "stress-ng", NULL };
    static const char *const sensors[] = { "lm_sensors", NULL };
    static const char *const ktools[] = { "kernel-tools", NULL };
    const char *virt = env_str("OSR_VIRT", "none");
    int ok;

    ok = osr_pkg_install_step("Installing benchmark workload", workload);

    /* A guest with no passthrough has no sensor to find, and no driver load or
     * probe will change that. Said once, plainly, instead of leaving three steps
     * to fail in a row and look like breakage. */
    if (strcmp(virt, "wsl") == 0 || strcmp(virt, "docker") == 0
        || strcmp(virt, "podman") == 0 || strcmp(virt, "lxc") == 0
        || strcmp(virt, "lxc-libvirt") == 0 || strcmp(virt, "systemd-nspawn") == 0) {
        osr_infof("%s guest: power and temperature sensors are not reachable from in here", virt);
        osr_info("throughput still measures normally - see: osr benchmark sensors");
    }

    /* try_step, not a bare pkg_install: an unwrapped install streams the package
     * manager's whole transcript -- several hundred lines -- straight into the
     * run, burying the two lines that are actually about the benchmark. Both of
     * these are cross-check conveniences, not requirements. */
    if (!osr_pkg_install_step_try("Installing sensors cross-check", sensors))
        osr_warn("lm_sensors not installed - the sensors(1) cross-check is unavailable");
    /* turbostat/perf live in a package whose name varies more than most, and on
     * several distros it is tied to the running kernel version. Not worth
     * failing over: nothing in osr calls them. */
    if (!osr_pkg_install_step_try("Installing kernel power tools", ktools))
        osr_info("kernel-tools not available here - not required");

    ok = osr_step("Loading CPU sensor drivers", load_sensor_modules, NULL) && ok;
    if (!osr_step_try("Keeping them loaded across reboots", persist_sensor_modules, NULL))
        osr_info("could not write /etc/modules-load.d - the drivers load per run instead");

    /* sensors-detect is what turns a board's super-I/O chip into hwmon nodes,
     * and on a desktop that chip is often the only thing measuring anything.
     * --auto answers every prompt with the safe default; it is still a probe of
     * unknown I/O ports, so it only runs when the tree is otherwise EMPTY --
     * where there is nothing to find by gentler means and nothing to lose. */
    if (osr_have_cmd("sensors-detect") && !have_hwmon()) {
        if (!osr_step_try("Probing for board sensors (sensors-detect --auto)",
                          sensors_detect, NULL))
            osr_info("sensors-detect found nothing - this board may have no readable sensors");
    }

    /* RAPL is the most accurate power source and is root-only on most kernels
     * since the PLATYPUS side channel (CVE-2020-8694). Say so once, here, rather
     * than leaving the user to wonder why `osr benchmark cpu` reports no power. */
    if (powercap_populated()) {
        if (access("/sys/class/powercap/intel-rapl:0/energy_uj", R_OK) == 0)
            osr_info("RAPL energy counter is readable - power measurement will work");
        else
            osr_info("RAPL present but root-only - run osr benchmark cpu with sudo for power numbers");
    } else {
        osr_info("no RAPL on this machine - the benchmark will fall back to hwmon or battery");
    }
    return ok;
}
