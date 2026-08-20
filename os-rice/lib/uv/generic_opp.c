/* lib/uv/generic_opp.c -- the last-resort backend: describe what this machine
 * exposes, and write nothing.
 *
 * This is what runs on an ARM SoC we do not have a device-tree recipe for, on
 * an x86 box where neither mailbox answered, and inside a VM. It exists
 * because the alternative -- printing "unsupported" and stopping -- throws away
 * the one thing the user actually needs at that moment, which is *why*, and
 * what the machine does have.
 *
 * It never writes. That is not timidity, it is the shape of the interfaces:
 * mainline Linux deliberately makes each regulator's `microvolts` attribute
 * read-only, and the OPP debugfs tree is a view rather than a control. On ARM
 * the voltage really does live in the device tree consumed at boot, which is
 * lib/uv/arm_dt.c's job and needs a reboot per change -- a fundamentally
 * different kind of operation, kept in a different file so nobody can perform
 * one while believing they are doing the other.
 *
 * What it reads:
 *
 *   /proc/cpuinfo                              vendor and model
 *   /sys/firmware/devicetree/base/compatible   the SoC, on ARM
 *   /sys/devices/system/cpu/cpufreq/policy*    frequency policies and driver
 *   /sys/kernel/debug/opp/                     operating point tables (root)
 *   /sys/class/regulator/                      the CPU rails and their limits
 *   /boot/...                                  which boot config an overlay
 *                                              would have to be registered in
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "backend.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

/* --- small sysfs readers -------------------------------------------------- */

/* read_trim -- a sysfs file's contents with surrounding whitespace removed.
 * Returns 1 when the file was readable. Sysfs values are short and
 * newline-terminated, which is the whole reason this is not just slurp. */
static int read_trim(Str *out, const char *path) {
    char *buf;
    size_t len, start, end;
    buf = slurp(path, &len);
    if (buf == NULL) return 0;
    start = 0;
    while (start < len && is_space(buf[start])) start++;
    end = len;
    while (end > start && is_space(buf[end - 1])) end--;
    str_add(out, buf + start, end - start);
    free(buf);
    return 1;
}

/* read_long -- read_trim plus strtol. Returns 1 on a clean whole-string parse;
 * a sysfs attribute that exists but holds junk reads as absent, because acting
 * on a half-parsed voltage is worse than acting on none. */
static int read_long(const char *path, long *out) {
    Str s;
    char *endp;
    long v;
    int ok = 0;
    str_init(&s);
    if (read_trim(&s, path) && s.len > 0) {
        v = strtol(str_text(&s), &endp, 10);
        if (*endp == '\0') {
            *out = v;
            ok = 1;
        }
    }
    str_free(&s);
    return ok;
}

/* path3 -- join into a caller-owned Str, so the readers above can be called on
 * a composed path without a fixed-size buffer anywhere. */
static void path3(Str *out, const char *a, const char *b, const char *c) {
    str_reset(out);
    str_addz(out, a);
    str_addz(out, b);
    str_addz(out, c);
}

/* --- report formatting ---------------------------------------------------- */

/* The report is a two-column list: a label and whatever we found. One shape,
 * so a long probe stays scannable. */
#define LABEL_WIDTH 14

static void row(Str *out, const char *label, const char *value) {
    size_t n;
    str_addz(out, "  ");
    str_addz(out, label);
    n = strlen(label);
    while (n < LABEL_WIDTH) {
        str_addc(out, ' ');
        n++;
    }
    str_addz(out, value);
    str_addc(out, '\n');
}

/* cont -- a continuation line under the previous row's value column. */
static void cont(Str *out, const char *value) {
    row(out, "", value);
}

/* --- the individual probes ------------------------------------------------ */

/* cpu_identity -- "AMD Ryzen 7 9800X3D 8-Core Processor (AuthenticAMD)", from
 * the first matching lines of /proc/cpuinfo. On ARM there is no model name, so
 * the caller falls back to the device-tree compatible string. */
static int cpu_identity(Str *out, Str *vendor_out) {
    char *buf;
    size_t len, pos = 0;
    Line ln;
    Str model, vendor;
    int found = 0;

    buf = slurp("/proc/cpuinfo", &len);
    if (buf == NULL) return 0;
    str_init(&model);
    str_init(&vendor);

    while (next_line(buf, len, &pos, &ln)) {
        const char *colon;
        const char *val;
        size_t klen;
        /* Each line is "key<tabs>: value"; we want two specific keys and the
         * first occurrence of each, since every core repeats them. */
        colon = memchr(ln.start, ':', ln.len);
        if (colon == NULL) continue;
        klen = (size_t)(colon - ln.start);
        while (klen > 0 && is_space(ln.start[klen - 1])) klen--;
        val = colon + 1;
        while (val < ln.start + ln.len && is_space(*val)) val++;

        if (model.len == 0 && klen == 10 && memcmp(ln.start, "model name", 10) == 0) {
            str_add(&model, val, (size_t)(ln.start + ln.len - val));
        } else if (vendor.len == 0 && klen == 9 && memcmp(ln.start, "vendor_id", 9) == 0) {
            str_add(&vendor, val, (size_t)(ln.start + ln.len - val));
        }
        if (model.len > 0 && vendor.len > 0) break;
    }
    free(buf);

    if (model.len > 0) {
        str_addz(out, str_text(&model));
        if (vendor.len > 0) {
            str_addz(out, " (");
            str_addz(out, str_text(&vendor));
            str_addc(out, ')');
        }
        found = 1;
    }
    if (vendor_out != NULL) str_addz(vendor_out, str_text(&vendor));
    str_free(&model);
    str_free(&vendor);
    return found;
}

/* dt_compatible -- the device tree's root compatible property, which is the
 * real SoC identity on ARM. It is a list of NUL-separated strings, most
 * specific first ("pine64,quartz64-a\0rockchip,rk3566\0"), so they are joined
 * rather than truncated at the first NUL the way a plain string read would. */
static int dt_compatible(Str *out) {
    char *buf;
    size_t len, i = 0;
    int n = 0;

    buf = slurp("/sys/firmware/devicetree/base/compatible", &len);
    if (buf == NULL) return 0;
    while (i < len) {
        size_t l = strlen(buf + i);
        if (l > 0) {
            if (n > 0) str_addz(out, ", ");
            str_addz(out, buf + i);
            n++;
        }
        i += l + 1;
    }
    free(buf);
    return n > 0;
}

/* probe_cpufreq -- one line per cpufreq policy: which driver owns it and the
 * frequency range it scales over. The driver name matters more than it looks:
 * "cpufreq-dt" or "cpufreq-rockchip" means the OPP table in the device tree is
 * the thing that holds the voltages, i.e. that arm_dt has something to work
 * with; "acpi-cpufreq"/"amd-pstate"/"intel_pstate" means voltage is the
 * firmware's business and no OPP table will be found.
 *
 * Returns the number of policies seen, and copies the first driver name into
 * driver_out for the verdict. */
static int probe_cpufreq(Str *report, Str *driver_out) {
    static const char *base = "/sys/devices/system/cpu/cpufreq/";
    DIR *d;
    struct dirent *e;
    Str path, val, line;
    int count = 0;

    d = opendir(base);
    if (d == NULL) {
        row(report, "cpufreq", "no /sys/devices/system/cpu/cpufreq (no scaling driver)");
        return 0;
    }
    str_init(&path);
    str_init(&val);
    str_init(&line);

    while ((e = readdir(d)) != NULL) {
        long lo = 0, hi = 0;
        if (strncmp(e->d_name, "policy", 6) != 0) continue;

        str_reset(&line);
        str_addz(&line, e->d_name);
        str_addz(&line, ": ");

        path3(&path, base, e->d_name, "/scaling_driver");
        str_reset(&val);
        if (read_trim(&val, str_text(&path))) {
            str_addz(&line, "driver=");
            str_addz(&line, str_text(&val));
            if (driver_out->len == 0) str_addz(driver_out, str_text(&val));
        } else {
            str_addz(&line, "driver=?");
        }

        path3(&path, base, e->d_name, "/cpuinfo_min_freq");
        if (read_long(str_text(&path), &lo)) {
            path3(&path, base, e->d_name, "/cpuinfo_max_freq");
            if (read_long(str_text(&path), &hi)) {
                str_addz(&line, ", ");
                str_addl(&line, lo / 1000);
                str_addz(&line, "-");
                str_addl(&line, hi / 1000);
                str_addz(&line, " MHz");
            }
        }

        path3(&path, base, e->d_name, "/scaling_governor");
        str_reset(&val);
        if (read_trim(&val, str_text(&path))) {
            str_addz(&line, ", governor=");
            str_addz(&line, str_text(&val));
        }

        row(report, count == 0 ? "cpufreq" : "", str_text(&line));
        count++;
    }
    closedir(d);

    if (count == 0) row(report, "cpufreq", "no policies");

    str_free(&path);
    str_free(&val);
    str_free(&line);
    return count;
}

/* probe_opp -- the OPP tables under debugfs. This is the closest thing to
 * "show me the voltage for each frequency" that the kernel offers, and it is
 * strictly a view: there is nothing writable in here. Root-only, and absent
 * entirely unless CONFIG_PM_OPP and debugfs are both in play, so all three of
 * "not mounted", "not permitted" and "no tables" are ordinary answers rather
 * than failures. */
static int probe_opp(Str *report) {
    static const char *base = "/sys/kernel/debug/opp/";
    DIR *d;
    struct dirent *e;
    Str line;
    int count = 0;

    if (!dir_exists("/sys/kernel/debug")) {
        row(report, "opp table", "debugfs not mounted - cannot enumerate operating points");
        return 0;
    }
    d = opendir(base);
    if (d == NULL) {
        row(report, "opp table",
            geteuid() == 0 ? "no /sys/kernel/debug/opp (kernel has no OPP tables)"
                           : "not readable as this user - re-run probe as root to see it");
        return 0;
    }
    str_init(&line);
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        str_reset(&line);
        str_addz(&line, e->d_name);
        row(report, count == 0 ? "opp table" : "", str_text(&line));
        count++;
    }
    closedir(d);
    if (count == 0) row(report, "opp table", "present but empty");
    str_free(&line);
    return count;
}

/* rail_is_cpuish -- does this regulator's name suggest it feeds the CPU? A
 * board exposes dozens of rails and listing them all buries the two that
 * matter, so the report leads with the likely ones and counts the rest.
 * Heuristic on purpose, and labelled as such in the output. */
static int rail_is_cpuish(const char *name) {
    return strstr(name, "cpu") != NULL
        || strstr(name, "core") != NULL
        || strstr(name, "vdd") != NULL
        || strstr(name, "vdec") != NULL;
}

/* probe_regulators -- the voltage rails, their current value and the range the
 * hardware itself declares. That range is the ceiling on any future undervolt:
 * min_microvolts is a floor the regulator will not go below no matter what a
 * device tree asks for, so it is the honest bound to show the user now. */
static int probe_regulators(Str *report) {
    static const char *base = "/sys/class/regulator/";
    DIR *d;
    struct dirent *e;
    Str path, name, line;
    int shown = 0, others = 0;

    d = opendir(base);
    if (d == NULL) {
        row(report, "regulators", "no /sys/class/regulator (no software-visible rails)");
        return 0;
    }
    str_init(&path);
    str_init(&name);
    str_init(&line);

    while ((e = readdir(d)) != NULL) {
        long now = 0, lo = 0, hi = 0;
        if (e->d_name[0] == '.') continue;

        str_reset(&name);
        path3(&path, base, e->d_name, "/name");
        if (!read_trim(&name, str_text(&path)) || name.len == 0) continue;
        if (!rail_is_cpuish(str_text(&name))) {
            others++;
            continue;
        }

        str_reset(&line);
        str_addz(&line, str_text(&name));
        str_addz(&line, ": ");
        path3(&path, base, e->d_name, "/microvolts");
        if (read_long(str_text(&path), &now)) {
            str_addl(&line, now / 1000);
            str_addz(&line, " mV");
        } else {
            str_addz(&line, "(voltage not exposed)");
        }
        path3(&path, base, e->d_name, "/min_microvolts");
        if (read_long(str_text(&path), &lo)) {
            path3(&path, base, e->d_name, "/max_microvolts");
            if (read_long(str_text(&path), &hi)) {
                str_addz(&line, ", hardware range ");
                str_addl(&line, lo / 1000);
                str_addz(&line, "-");
                str_addl(&line, hi / 1000);
                str_addz(&line, " mV");
            }
        }
        row(report, shown == 0 ? "regulators" : "", str_text(&line));
        shown++;
    }
    closedir(d);

    if (shown == 0 && others == 0) {
        row(report, "regulators", "none");
    } else if (shown == 0) {
        str_reset(&line);
        str_addl(&line, others);
        str_addz(&line, " rail(s), none named like a CPU rail");
        row(report, "regulators", str_text(&line));
    } else if (others > 0) {
        str_reset(&line);
        str_addz(&line, "(+ ");
        str_addl(&line, others);
        str_addz(&line, " other rails not shown; names above are matched by heuristic)");
        cont(report, str_text(&line));
    }

    str_free(&path);
    str_free(&name);
    str_free(&line);
    return shown;
}

/* probe_bootcfg -- which boot configuration file an OPP overlay would have to
 * be registered in for arm_dt to ever work here. Reported now because it is
 * the difference between "this SoC is tunable in principle" and "and we have
 * somewhere to put the answer". */
static int probe_bootcfg(Str *report) {
    static const char *const candidates[] = {
        "/boot/armbianEnv.txt",
        "/boot/firmware/config.txt",
        "/boot/config.txt",
        "/boot/extlinux/extlinux.conf",
        "/boot/firmware/extlinux/extlinux.conf"
    };
    size_t i;
    int found = 0;
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (file_exists(candidates[i])) {
            row(report, found == 0 ? "boot config" : "", candidates[i]);
            found++;
        }
    }
    if (found == 0) row(report, "boot config", "none found - no place to register a DT overlay");
    return found;
}

/* --- the backend ---------------------------------------------------------- */

static int generic_probe(UvCaps *caps, Str *report) {
    Str id, driver, vendor;
    int have_opp, have_rails, dt;

    caps->backend = "generic-opp";
    /* Everything stays at uv_caps_init's zeroes: no plane is writable, which
     * is the entire point of this backend. */
    strcpy(caps->detail, "read-only: no voltage-control interface on this machine");

    str_init(&id);
    str_init(&driver);
    str_init(&vendor);

    str_addz(report, "\n");
    if (cpu_identity(&id, &vendor)) {
        row(report, "cpu", str_text(&id));
    } else {
        row(report, "cpu", "(no model name in /proc/cpuinfo)");
    }
    str_reset(&id);
    dt = dt_compatible(&id);
    if (dt) row(report, "soc", str_text(&id));

    probe_cpufreq(report, &driver);
    have_opp = probe_opp(report);
    have_rails = probe_regulators(report);
    if (dt) probe_bootcfg(report);

    /* The verdict. This is the part the user came for, so it says what is not
     * possible and why, not merely that something failed. */
    str_addz(report, "\n");
    row(report, "verdict", "no writable voltage plane - nothing to tune here");
    if (dt && (have_opp || have_rails)) {
        cont(report, "This SoC keeps its voltages in the device-tree OPP table.");
        cont(report, "Neither the regulator sysfs nor the OPP debugfs is writable");
        cont(report, "by design, so changing them means rebuilding the DT overlay");
        cont(report, "consumed at boot - one reboot per step, and a bad value");
        cont(report, "survives the reboot. That is `osr undervolt` step 7 and is");
        cont(report, "not wired up yet.");
    } else if (strcmp(str_text(&vendor), "AuthenticAMD") == 0) {
        cont(report, "The AMD backend did not claim this machine. On real Ryzen");
        cont(report, "hardware that nearly always means the ryzen_smu driver is");
        cont(report, "not loaded - Curve Optimizer is reached through it, and");
        cont(report, "there is no other userspace path. Install it (`osr module");
        cont(report, "undervolt`), `modprobe ryzen_smu`, and probe again.");
        cont(report, "Inside a VM it means the host never passed the SMU through,");
        cont(report, "and no driver will change that.");
    } else if (strcmp(str_text(&vendor), "GenuineIntel") == 0) {
        cont(report, "The Intel backend did not claim this machine: MSR 0x150 is");
        cont(report, "absent, the msr module is not loaded, or firmware has set");
        cont(report, "the overclocking-lock bit. That last one is the common case");
        cont(report, "on anything sold after the 2019 Plundervolt mitigation and");
        cont(report, "is not something software can undo.");
    } else {
        cont(report, "No vendor mailbox answered and there is no device tree, so");
        cont(report, "voltage here is firmware's business.");
    }

    str_free(&id);
    str_free(&driver);
    str_free(&vendor);
    return 1; /* always claims: this is the last resort */
}

static int generic_read(UvDomain d, int idx, int *mv) {
    (void)d;
    (void)idx;
    (void)mv;
    return UV_ENOREAD;
}

static int generic_write(UvDomain d, int idx, int mv) {
    (void)d;
    (void)idx;
    (void)mv;
    return UV_ERR; /* never, under any circumstances */
}

static int generic_reset(void) {
    /* Nothing was ever applied, so "everything back to stock" is already true.
     * Succeeding here rather than failing matters: the engine calls reset() on
     * every recovery path, and a backend that cannot write must not turn that
     * into an error. */
    return UV_OK;
}

const UvBackend uv_backend_generic_opp = {
    "generic-opp",
    generic_probe,
    generic_read,
    generic_write,
    generic_reset
};
