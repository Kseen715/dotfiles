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
/* rapl_driver_loaded -- is the powercap RAPL driver actually in the kernel?
 *
 * /sys/module holds built-ins as well as loaded modules, so this answers "is
 * the code there", which is the question. Either name counts: intel_rapl_msr
 * is the MSR front end and intel_rapl_common is what registers the powercap
 * domains, and a kernel may have either built in. */
static int rapl_driver_loaded(void) {
    static const char *const names[] = { "intel_rapl_msr", "intel_rapl_common", "intel_rapl" };
    const char *base = env_str("OSR_SYSMODULE", "/sys/module/");
    Str path;
    size_t i;
    int found = 0;

    str_init(&path);
    for (i = 0; !found && i < sizeof(names) / sizeof(names[0]); i++) {
        str_reset(&path);
        str_addz(&path, base);
        str_addz(&path, names[i]);
        found = dir_exists(str_text(&path));
    }
    str_free(&path);
    return found;
}

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
        /* An empty tree has three different causes, and telling them apart is
         * the difference between a fix and a wild goose chase:
         *
         *   the driver is not loaded    loading it works. The common case.
         *   the driver IS loaded        it probed and registered nothing, so
         *                               this CPU has no RAPL at all. Intel
         *                               introduced it with Sandy Bridge, so
         *                               anything older -- Westmere, Nehalem,
         *                               Core 2 -- lands here permanently, and
         *                               being told to load a module that is
         *                               already loaded is maddening.
         *   a Hyper-V guest             the MSRs are not passed through.
         */
        if (bench_is_wsl()) {
            bench_set_str(m->detail, sizeof(m->detail),
                    "no sensor in a WSL guest - Hyper-V does not pass the RAPL MSRs through");
        } else if (rapl_driver_loaded()) {
            bench_set_str(m->detail, sizeof(m->detail),
                    "no RAPL on this CPU - the driver is loaded and found nothing "
                    "(Intel RAPL needs Sandy Bridge or newer)");
        } else {
            bench_set_str(m->detail, sizeof(m->detail),
                    "powercap tree is empty - the intel_rapl_msr driver is not loaded "
                    "(run: osr module benchmark)");
        }
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
 * status is checked rather than assumed.
 *
 * Two shapes of battery. Most publish `power_now` in microwatts and there is
 * nothing to do. Older ACPI batteries -- and most laptops from before about
 * 2012 -- publish `current_now` and `voltage_now` instead, and the power is
 * their product. Reading only the first shape means a machine whose ONLY
 * possible power source is its battery reports nothing at all.
 */
static int detect_battery(PwrMeter *m) {
    const char *base = env_str("OSR_POWER_SUPPLY", "/sys/class/power_supply/");
    DIR *d;
    struct dirent *e;
    Str path, status;
    int found = 0;
    int on_ac = 0;

    d = opendir(base);
    if (d == NULL) return 0;
    str_init(&path);
    str_init(&status);

    while (!found && (e = readdir(d)) != NULL) {
        unsigned long probe, volts;
        if (strncmp(e->d_name, "BAT", 3) != 0) continue;

        str_reset(&status);
        bench_join3(&path, base, e->d_name, "/status");
        if (!bench_read_trim(&status, str_text(&path))) continue;
        if (strcmp(str_text(&status), "Discharging") != 0) {
            on_ac = 1;
            continue;
        }

        bench_join3(&path, base, e->d_name, "/power_now");
        if (bench_read_ulong(str_text(&path), &probe) && probe > 0) {
            bench_set_str(m->path, sizeof(m->path), str_text(&path));
            m->path_v[0] = '\0';
            m->source = PWR_BATTERY;
            bench_set_str(m->detail, sizeof(m->detail),
                    "battery discharge rate - whole system, not just the CPU");
            found = 1;
            continue;
        }

        bench_join3(&path, base, e->d_name, "/current_now");
        if (!bench_read_ulong(str_text(&path), &probe) || probe == 0) continue;
        bench_set_str(m->path, sizeof(m->path), str_text(&path));
        bench_join3(&path, base, e->d_name, "/voltage_now");
        if (!bench_read_ulong(str_text(&path), &volts) || volts == 0) continue;
        bench_set_str(m->path_v, sizeof(m->path_v), str_text(&path));
        m->source = PWR_BATTERY;
        bench_set_str(m->detail, sizeof(m->detail),
                "battery current x voltage - whole system, not just the CPU");
        found = 1;
    }
    closedir(d);
    str_free(&path);
    str_free(&status);

    if (!found && on_ac) {
        /* Named as an INSTRUCTION, not just a state. On a laptop with no RAPL
         * and no hwmon rail this is the only power figure the machine can
         * ever produce, and "unplug it" is the whole fix. */
        bench_set_str(m->detail, sizeof(m->detail),
                "battery is charging - unplug AC and re-run to measure discharge power");
    }
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

/* pwr_probe_report -- ask every source separately and print what each said.
 *
 * pwr_detect stops at the first source that answers and leaves one explanation
 * behind, which on a machine where nothing answers is simply whichever probe
 * ran last. A laptop on AC therefore reported "battery not discharging" and
 * never mentioned that it also has no RAPL and no hwmon rail -- three
 * different facts, one of which was actionable, and the wrong one shown. */
void pwr_probe_report(Str *out) {
    PwrMeter m;
    struct { const char *label; int (*probe)(PwrMeter *); } sources[3];
    size_t i;

    sources[0].label = "rapl";    sources[0].probe = detect_rapl;
    sources[1].label = "hwmon";   sources[1].probe = detect_hwmon;
    sources[2].label = "battery"; sources[2].probe = detect_battery;

    for (i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        memset(&m, 0, sizeof(m));
        m.source = PWR_NONE;
        if (sources[i].probe(&m)) {
            Str v;
            str_init(&v);
            str_addz(&v, "FOUND - ");
            str_addz(&v, m.detail);
            bench_row(out, sources[i].label, str_text(&v));
            str_free(&v);
        } else {
            bench_row(out, sources[i].label,
                      m.detail[0] != '\0' ? m.detail : "no such sensor");
        }
    }
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
    if (m->path_v[0] != '\0') {
        /* microamps x microvolts, so the product is picowatts. Done in double
         * because 3_000_000 uA x 12_000_000 uV overflows 32-bit long by four
         * orders of magnitude. */
        unsigned long uv;
        if (!bench_read_ulong(m->path_v, &uv)) return;
        m->sum_w += ((double)uw * (double)uv) / 1e12;
    } else {
        m->sum_w += (double)uw / 1e6;
    }
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
