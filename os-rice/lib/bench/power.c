/* lib/bench/power.c -- finding and reading a power sensor. See bench.h for why
 * this is layered rather than a single path.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "bench.h"

#include <dirent.h>
#include <unistd.h>

static const char *const source_names[PWR_SOURCE_MAX] = {
    "none", "rapl", "hwmon", "ryzen-smu", "battery"
};

const char *pwr_source_name(PwrSource s) {
    if (s < 0 || s >= PWR_SOURCE_MAX) return "?";
    return source_names[s];
}

/* --- pure arithmetic ------------------------------------------------------ */

unsigned long pwr_energy_delta(unsigned long a, unsigned long b, unsigned long max_range) {
    if (b >= a) return b - a;
    /* Wrapped. With a known range the lost span is exact; without one the best
     * guess is that it wrapped once at the type's width, and guessing wrong
     * here is better than reporting a negative energy. */
    if (max_range > 0) return (max_range - a) + b;
    return (0xFFFFFFFFUL - a) + b + 1;
}

/* --- detection ------------------------------------------------------------ */

/* RAPL. The powercap tree names the Intel driver's nodes "intel-rapl:N" even
 * on AMD parts, so the directory name is not the thing to match on -- the
 * `name` attribute is.
 *
 * Which domain matters. A socket publishes several, and they measure
 * different things:
 *
 *   package-N  the whole socket. What "CPU power" means, and what every other
 *              tool reports. Preferred.
 *   psys       the PLATFORM domain, present on most Intel laptops since
 *              Skylake: the whole board's budget, CPU included. Wider than
 *              asked for, but a real measurement and far better than nothing
 *              -- so it is taken when there is no package domain, and the
 *              report says which one it was.
 *   core/uncore/dram  children ("intel-rapl:0:1"), each a fraction of the
 *              package. Reporting one as the package power would understate
 *              the machine several-fold, so they are skipped entirely.
 */
static int rapl_rank(const char *name) {
    if (strncmp(name, "package", 7) == 0) return 3;
    if (strcmp(name, "psys") == 0) return 2;
    return 0;
}

static int detect_rapl(PwrMeter *m) {
    /* Overridable so the fixtures in test/unit_c/bench_test.c can present an
     * Intel box with an unloaded driver, a root-only counter, or a psys-only
     * laptop -- none of which the machine running the tests is. Same device
     * lib/detect.c uses for OSR_MEMINFO/OSR_DRM. */
    const char *base = env_str("OSR_POWERCAP", "/sys/class/powercap/");
    DIR *d;
    struct dirent *e;
    Str path, name, best_path;
    int best = 0;
    int entries = 0;         /* domains present, readable or not */
    int denied = 0;          /* ...of which some were unreadable */

    d = opendir(base);
    if (d == NULL) {
        /* No powercap tree at all: CONFIG_POWERCAP is off, or this is a guest
         * whose kernel never had it. Distinct from "empty", which is a driver
         * that merely has not been loaded. */
        bench_set_str(m->detail, sizeof(m->detail),
                "no /sys/class/powercap - this kernel has no powercap support");
        return 0;
    }
    str_init(&path);
    str_init(&name);
    str_init(&best_path);

    while ((e = readdir(d)) != NULL) {
        unsigned long probe;
        int rank;
        if (e->d_name[0] == '.') continue;
        /* Only top-level domains: a child looks like "intel-rapl:0:1". */
        if (strchr(e->d_name, ':') == NULL) continue;
        if (strchr(strchr(e->d_name, ':') + 1, ':') != NULL) continue;
        entries++;

        str_reset(&name);
        bench_join3(&path, base, e->d_name, "/name");
        if (!bench_read_trim(&name, str_text(&path))) continue;
        rank = rapl_rank(str_text(&name));
        if (rank <= best) continue;

        bench_join3(&path, base, e->d_name, "/energy_uj");
        if (!bench_read_ulong(str_text(&path), &probe)) {
            /* Present but unreadable is the common case: since the PLATYPUS
             * side channel (CVE-2020-8694) these are 0400. Worth saying so
             * explicitly, because "run it as root" is an actionable answer. */
            denied++;
            continue;
        }

        best = rank;
        str_reset(&best_path);
        str_addz(&best_path, str_text(&path));
        m->max_range_uj = 0;
        bench_join3(&path, base, e->d_name, "/max_energy_range_uj");
        bench_read_ulong(str_text(&path), &m->max_range_uj);
        str_reset(&path);
        str_addz(&path, rank == 3 ? "RAPL package energy counter"
                                  : "RAPL psys (platform) energy counter - whole board, not just the CPU");
        bench_set_str(m->detail, sizeof(m->detail), str_text(&path));
    }
    closedir(d);

    if (best > 0) {
        bench_set_str(m->path, sizeof(m->path), str_text(&best_path));
        m->source = PWR_RAPL;
    } else if (denied > 0) {
        bench_set_str(m->detail, sizeof(m->detail),
                geteuid() == 0 ? "RAPL present but energy_uj unreadable"
                               : "RAPL present but root-only - re-run as root for power numbers");
    } else if (entries == 0) {
        /* The tree exists and is empty. On bare metal that is nearly always
         * the one fixable cause of "no power": the powercap RAPL driver is a
         * module and nothing has loaded it. `osr module benchmark` does.
         *
         * In a Hyper-V guest the tree is empty for a reason no modprobe can
         * fix -- the RAPL MSRs are not passed through -- so the same symptom
         * gets the opposite advice. Same check the diagnostic makes. */
        bench_set_str(m->detail, sizeof(m->detail),
                bench_is_wsl()
                    ? "no sensor in a WSL guest - Hyper-V does not pass the RAPL MSRs through"
                    : "powercap tree is empty - the intel_rapl_msr driver is not loaded "
                      "(run: osr module benchmark)");
    } else {
        bench_set_str(m->detail, sizeof(m->detail),
                "powercap has no package or psys domain - only per-domain children");
    }

    str_free(&path);
    str_free(&name);
    str_free(&best_path);
    return best > 0;
}

/* hwmon, in microwatts. Instantaneous, so it needs polling.
 *
 * Not just `power1_input`. The hwmon ABI defines both `powerN_input` (the
 * instantaneous reading) and `powerN_average` (the chip's own averaging
 * window), and which one a driver publishes is the driver's choice -- several
 * super-I/O and BMC chips offer only the average. Looking for one name was
 * enough on the desks this was written on and reports nothing on a board that
 * made the other choice.
 *
 * The index is scanned too: on a board with several rails the CPU one is not
 * reliably first, and a partial reading beats no reading. Nothing here can
 * tell which rail is which, so the attribute found first wins and the report
 * names the driver it came from -- honest about what it is rather than
 * claiming to be the package.
 */
static int detect_hwmon(PwrMeter *m) {
    const char *base = env_str("OSR_HWMON", "/sys/class/hwmon/");
    static const char *const attrs[] = { "input", "average" };
    DIR *d;
    struct dirent *e;
    Str path, name, found_at;
    int found = 0;

    d = opendir(base);
    if (d == NULL) return 0;
    str_init(&path);
    str_init(&name);
    str_init(&found_at);

    while (!found && (e = readdir(d)) != NULL) {
        unsigned long probe;
        int idx;
        size_t a;
        if (e->d_name[0] == '.') continue;

        for (idx = 1; !found && idx <= 3; idx++) {
            for (a = 0; a < sizeof(attrs) / sizeof(attrs[0]); a++) {
                Str leaf;
                str_init(&leaf);
                str_addz(&leaf, "/power");
                str_addl(&leaf, idx);
                str_addc(&leaf, '_');
                str_addz(&leaf, attrs[a]);
                bench_join3(&path, base, e->d_name, str_text(&leaf));
                str_free(&leaf);
                if (!bench_read_ulong(str_text(&path), &probe)) continue;

                str_reset(&found_at);
                str_addz(&found_at, str_text(&path));
                found = 1;
                break;
            }
        }
        if (!found) continue;

        str_reset(&name);
        bench_set_str(m->path, sizeof(m->path), str_text(&found_at));
        bench_join3(&path, base, e->d_name, "/name");
        bench_read_trim(&name, str_text(&path));

        m->source = PWR_HWMON;
        str_reset(&path);
        str_addz(&path, "hwmon power sensor");
        if (name.len > 0) {
            str_addz(&path, " (");
            str_addz(&path, str_text(&name));
            str_addc(&path, ')');
        }
        bench_set_str(m->detail, sizeof(m->detail), str_text(&path));
    }
    closedir(d);
    str_free(&path);
    str_free(&name);
    str_free(&found_at);
    return found;
}

/* Battery discharge. Only meaningful while actually discharging -- on AC the
 * reading is 0 or absent, which would silently report a 0 W CPU, so the
 * status is checked rather than assumed. */
static int detect_battery(PwrMeter *m) {
    static const char *base = "/sys/class/power_supply/";
    DIR *d;
    struct dirent *e;
    Str path, status;
    int found = 0;

    d = opendir(base);
    if (d == NULL) return 0;
    str_init(&path);
    str_init(&status);

    while (!found && (e = readdir(d)) != NULL) {
        unsigned long probe;
        if (strncmp(e->d_name, "BAT", 3) != 0) continue;

        str_reset(&status);
        bench_join3(&path, base, e->d_name, "/status");
        if (!bench_read_trim(&status, str_text(&path))) continue;
        if (strcmp(str_text(&status), "Discharging") != 0) {
            bench_set_str(m->detail, sizeof(m->detail),
                    "battery present but not discharging - no power reading on AC");
            continue;
        }
        bench_join3(&path, base, e->d_name, "/power_now");
        if (!bench_read_ulong(str_text(&path), &probe)) continue;

        bench_set_str(m->path, sizeof(m->path), str_text(&path));
        m->source = PWR_BATTERY;
        bench_set_str(m->detail, sizeof(m->detail),
                "battery discharge rate - whole system, not just the CPU");
        found = 1;
    }
    closedir(d);
    str_free(&path);
    str_free(&status);
    return found;
}

int pwr_detect(PwrMeter *m) {
    memset(m, 0, sizeof(*m));
    m->source = PWR_NONE;
    /* Order is by accuracy, not convenience. */
    if (detect_rapl(m)) return 1;
    if (detect_hwmon(m)) return 1;
    if (detect_battery(m)) return 1;
    if (m->detail[0] == '\0') {
        bench_set_str(m->detail, sizeof(m->detail),
                "no power sensor: no RAPL, no hwmon power input, no battery");
    }
    return 0;
}

/* --- measuring ------------------------------------------------------------ */

void pwr_begin(PwrMeter *m) {
    m->sum_w = 0.0;
    m->samples = 0;
    m->e_start = 0;
    m->t_start = bench_now_sec();
    if (m->source == PWR_RAPL) bench_read_ulong(m->path, &m->e_start);
}

void pwr_sample(PwrMeter *m) {
    unsigned long uw;
    if (m->source != PWR_HWMON && m->source != PWR_BATTERY) return;
    if (!bench_read_ulong(m->path, &uw)) return;
    m->sum_w += (double)uw / 1e6;
    m->samples++;
}

int pwr_end(PwrMeter *m, double *watts) {
    double elapsed = bench_now_sec() - m->t_start;

    if (m->source == PWR_RAPL) {
        unsigned long e_end = 0;
        if (elapsed <= 0.0) return 0;
        if (!bench_read_ulong(m->path, &e_end)) return 0;
        *watts = ((double)pwr_energy_delta(m->e_start, e_end, m->max_range_uj) / 1e6) / elapsed;
        return 1;
    }
    if (m->samples > 0) {
        *watts = m->sum_w / (double)m->samples;
        return 1;
    }
    return 0;
}
