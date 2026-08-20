/* lib/bench/cpu.c -- the workload, the polling loop, and the two output forms.
 *
 * The shape of a measurement here is always the same: start the meter, fork the
 * workload, poll at ~4 Hz until it exits, stop the meter. Polling is what makes
 * an instantaneous power sensor usable at all, and it is also where peak
 * temperature and peak clock come from -- both are "what was the highest value
 * seen while busy", which cannot be answered by reading once at the end.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "bench.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define POLL_NS 250000000L   /* 250 ms: fine enough to catch a boost spike,
                              * coarse enough that the poller costs nothing */

/* --- sensors -------------------------------------------------------------- */

/* find_cpu_temp -- the hwmon input most likely to be the CPU package.
 *
 * Resolved once and cached, because doing this per poll would mean opening the
 * whole hwmon tree four times a second. The driver names are the reliable
 * signal: k10temp/zenpower on AMD, coretemp on Intel, cpu_thermal on ARM.
 */
static int find_cpu_temp(char *out, size_t cap) {
    static const char *const drivers[] = {
        "k10temp", "zenpower", "coretemp", "cpu_thermal", "cpu-thermal"
    };
    static const char *base = "/sys/class/hwmon/";
    DIR *d;
    struct dirent *e;
    Str path, name;
    int found = 0;
    size_t i;

    d = opendir(base);
    if (d == NULL) return 0;
    str_init(&path);
    str_init(&name);

    while (!found && (e = readdir(d)) != NULL) {
        long probe;
        if (e->d_name[0] == '.') continue;
        str_reset(&name);
        str_reset(&path);
        str_addz(&path, base);
        str_addz(&path, e->d_name);
        str_addz(&path, "/name");
        if (!bench_read_trim(&name, str_text(&path))) continue;

        for (i = 0; i < sizeof(drivers) / sizeof(drivers[0]); i++) {
            if (strcmp(str_text(&name), drivers[i]) == 0) break;
        }
        if (i == sizeof(drivers) / sizeof(drivers[0])) continue;

        str_reset(&path);
        str_addz(&path, base);
        str_addz(&path, e->d_name);
        str_addz(&path, "/temp1_input");
        if (!bench_read_long(str_text(&path), &probe)) continue;
        bench_set_str(out, cap, str_text(&path));
        found = 1;
    }
    closedir(d);
    str_free(&path);
    str_free(&name);
    return found;
}

/* sample_freq -- the highest scaling_cur_freq across all CPUs, in kHz. Absent
 * on machines with no cpufreq driver, which is not an error. */
static int sample_freq(long *out) {
    static const char *base = "/sys/devices/system/cpu/";
    DIR *d;
    struct dirent *e;
    Str path;
    long best = 0;
    int found = 0;

    d = opendir(base);
    if (d == NULL) return 0;
    str_init(&path);
    while ((e = readdir(d)) != NULL) {
        long v;
        if (strncmp(e->d_name, "cpu", 3) != 0) continue;
        if (e->d_name[3] < '0' || e->d_name[3] > '9') continue;
        str_reset(&path);
        str_addz(&path, base);
        str_addz(&path, e->d_name);
        str_addz(&path, "/cpufreq/scaling_cur_freq");
        if (!bench_read_long(str_text(&path), &v)) continue;
        if (v > best) best = v;
        found = 1;
    }
    closedir(d);
    str_free(&path);
    if (found) *out = best;
    return found;
}

/* --- the polled child ----------------------------------------------------- */

typedef struct {
    PwrMeter *meter;
    BenchResult *r;
    const char *temp_path;   /* "" when there is no sensor */
} Poller;

static void poll_once(Poller *p) {
    long v;
    pwr_sample(p->meter);
    if (p->temp_path[0] != '\0' && bench_read_long(p->temp_path, &v)) {
        double c = (double)v / 1000.0;   /* hwmon reports millidegrees */
        if (!p->r->have_temp || c > p->r->peak_temp_c) {
            p->r->peak_temp_c = c;
            p->r->have_temp = 1;
        }
    }
    if (sample_freq(&v)) {
        if (!p->r->have_freq || v > p->r->peak_freq_khz) {
            p->r->peak_freq_khz = v;
            p->r->have_freq = 1;
        }
    }
}

static void nap(void) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = POLL_NS;
    nanosleep(&ts, NULL);
}

/* run_polled -- fork argv, poll while it runs, return its exit status (or -1).
 *
 * The child's output goes to /dev/null unless --verbose: the whole point of the
 * default output is that it is short, and stress-ng is not. */
static int run_polled(char *const argv[], Poller *p, int verbose) {
    pid_t pid;
    int status = 0;

    pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (!verbose) {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, 1);
                dup2(devnull, 2);
                if (devnull > 2) close(devnull);
            }
        }
        execvp(argv[0], argv);
        _exit(127);
    }

    for (;;) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        if (done < 0) return -1;
        nap();
        poll_once(p);
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

/* --- stress-ng ------------------------------------------------------------ */

int bench_parse_yaml_ops(const char *buf, size_t len, double *ops) {
    static const char *key = "bogo-ops-per-second-real-time:";
    size_t klen = strlen(key);
    size_t pos = 0;
    Line ln;
    double total = 0.0;
    int found = 0;

    while (next_line(buf, len, &pos, &ln)) {
        const char *p = ln.start;
        const char *end = ln.start + ln.len;
        char num[64];
        size_t n = 0;
        char *endp;

        while (p < end && is_space(*p)) p++;
        if (p < end && *p == '-') { /* the list marker on an entry's first key */
            p++;
            while (p < end && is_space(*p)) p++;
        }
        if ((size_t)(end - p) < klen || memcmp(p, key, klen) != 0) continue;
        p += klen;
        while (p < end && is_space(*p)) p++;
        while (p < end && n + 1 < sizeof(num) && !is_space(*p)) num[n++] = *p++;
        num[n] = '\0';
        if (n == 0) continue;
        {
            /* Validate before accumulating: a malformed value must leave the
             * total untouched, not contribute whatever strtod salvaged. */
            double v = strtod(num, &endp);
            if (*endp != '\0') continue;
            total += v;
            found = 1;
        }
    }
    if (found) *ops = total;
    return found;
}

/* run_stressng -- one phase. cpus is 0 for "all". */
static int run_stressng(int cpus, int seconds, Poller *p, int verbose, double *ops) {
    char yaml_path[] = "/tmp/osr-bench-XXXXXX";
    char cpus_buf[16], time_buf[16];
    char *argv[12];
    int fd, i = 0, rc;
    char *buf;
    size_t len;
    int got = 0;

    fd = mkstemp(yaml_path);
    if (fd < 0) return 0;
    close(fd);

    sprintf(cpus_buf, "%d", cpus);
    sprintf(time_buf, "%ds", seconds);

    argv[i++] = (char *)"stress-ng";
    argv[i++] = (char *)"--cpu";
    argv[i++] = cpus_buf;
    argv[i++] = (char *)"--cpu-method";
    argv[i++] = (char *)"matrixprod";
    argv[i++] = (char *)"--metrics-brief";
    argv[i++] = (char *)"-t";
    argv[i++] = time_buf;
    argv[i++] = (char *)"--yaml";
    argv[i++] = yaml_path;
    argv[i] = NULL;

    rc = run_polled(argv, p, verbose);
    if (rc == 0) {
        buf = slurp(yaml_path, &len);
        if (buf != NULL) {
            got = bench_parse_yaml_ops(buf, len, ops);
            free(buf);
        }
    }
    unlink(yaml_path);
    return got;
}

/* --- identity ------------------------------------------------------------- */

/* cpu_model -- /proc/cpuinfo's `model name`, squeezed.
 *
 * Not read from $OSR_CPU_MODEL: `osr benchmark cpu` runs without a detection
 * pass in front of it, so the environment is usually empty here. It gets the
 * same squeeze lib/detect.c applies, for the same reason -- Intel's brand
 * string carries its own padding -- so the two agree on one machine's name. */
static void cpu_model(char *out, size_t cap) {
    char *buf;
    size_t len, pos = 0;
    Line ln;
    Str name;

    bench_set_str(out, cap, "(unknown)");
    buf = slurp("/proc/cpuinfo", &len);
    if (buf == NULL) return;
    while (next_line(buf, len, &pos, &ln)) {
        const char *colon = memchr(ln.start, ':', ln.len);
        size_t klen;
        const char *val;
        if (colon == NULL) continue;
        klen = (size_t)(colon - ln.start);
        while (klen > 0 && is_space(ln.start[klen - 1])) klen--;
        if (klen != 10 || memcmp(ln.start, "model name", 10) != 0) continue;
        val = colon + 1;
        str_init(&name);
        str_add_squeezed(&name, val, (size_t)(ln.start + ln.len - val));
        if (name.len > 0) bench_set_str(out, cap, str_text(&name));
        str_free(&name);
        break;
    }
    free(buf);
}

static int cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

/* --- deps ----------------------------------------------------------------- */

int bench_deps_missing(Str *out) {
    /* Only stress-ng is required. sensors/turbostat are conveniences the
     * module installs, but this code reads sysfs directly and does not shell
     * out to them, so their absence changes nothing. */
    int missing = 0;
    if (!osr_path_lookup("stress-ng", NULL)) {
        str_addz(out, "stress-ng");
        missing++;
    }
    return missing;
}

/* --- the run -------------------------------------------------------------- */

void bench_result_init(BenchResult *r) {
    memset(r, 0, sizeof(*r));
    bench_set_str(r->cpu_model, sizeof(r->cpu_model), "(unknown)");
    bench_set_str(r->power_detail, sizeof(r->power_detail), "not measured");
}

/* phase -- "[1/3] idle power - 5s, no load (RAPL)".
 *
 * One line per phase, printed as it starts. The alternative -- a single line up
 * front saying how long the whole thing takes -- leaves a minute of silence
 * during which the only evidence of what is happening is the fans, and no way
 * to tell a slow all-core phase from a hung one. */
static void phase(int on, int n, int total, const char *what,
                  int secs, const char *detail) {
    Str m;
    if (!on) return;
    str_init(&m);
    str_addc(&m, '[');
    str_addl(&m, n);
    str_addc(&m, '/');
    str_addl(&m, total);
    str_addz(&m, "] ");
    str_addz(&m, what);
    str_addz(&m, " - ");
    str_addl(&m, secs);
    str_addz(&m, "s, ");
    str_addz(&m, detail);
    osr_info(str_text(&m));
    str_free(&m);
}

int bench_cpu(const BenchOpts *o, BenchResult *r) {
    PwrMeter meter;
    Poller p;
    char temp_path[BENCH_PATH_MAX];
    Str detail;
    double t0;
    int idle_secs;
    int nphase, phase_n = 0;

    bench_result_init(r);
    cpu_model(r->cpu_model, sizeof(r->cpu_model));
    r->ncpu = cpu_count();

    temp_path[0] = '\0';
    find_cpu_temp(temp_path, sizeof(temp_path));

    pwr_detect(&meter);
    bench_set_str(r->power_detail, sizeof(r->power_detail), meter.detail);

    p.meter = &meter;
    p.r = r;
    p.temp_path = temp_path;

    t0 = bench_now_sec();

    /* Idle first, and before anything has heated the part up -- an idle figure
     * taken after the load phases would be measuring the cooldown, not idle. */
    idle_secs = o->seconds / 4;
    if (idle_secs < 3) idle_secs = 3;
    /* The idle phase only happens when there is a meter to read during it, so
     * the denominator has to say 2 on a machine without one rather than
     * promising a third phase that never runs. */
    nphase = (meter.source != PWR_NONE) ? 3 : 2;
    if (meter.source != PWR_NONE) {
        int i;
        phase(o->announce, ++phase_n, nphase, "idle power", idle_secs,
              "machine at rest");
        pwr_begin(&meter);
        for (i = 0; i < idle_secs * 4; i++) {
            nap();
            pwr_sample(&meter);
        }
        if (pwr_end(&meter, &r->idle_w)) r->have_power = 1;
    }

    /* Single core: what one thread can do, which is the number that moves when
     * boost behaviour changes. */
    phase(o->announce, ++phase_n, nphase, "single-core throughput", o->seconds,
          "stress-ng matrixprod on 1 thread");
    pwr_begin(&meter);
    if (run_stressng(1, o->seconds, &p, o->verbose, &r->single_ops)) r->have_single = 1;

    /* All cores: the sustained figure, and the one the power reading belongs
     * to -- peak temperature and clock are captured across both phases. */
    str_init(&detail);
    str_addz(&detail, "stress-ng matrixprod on ");
    str_addl(&detail, r->ncpu);
    str_addz(&detail, r->ncpu == 1 ? " thread" : " threads");
    phase(o->announce, ++phase_n, nphase, "all-core throughput", o->seconds,
          str_text(&detail));
    str_free(&detail);
    pwr_begin(&meter);
    if (run_stressng(0, o->seconds, &p, o->verbose, &r->all_ops)) r->have_all = 1;
    if (r->have_power) {
        double w;
        if (pwr_end(&meter, &w)) r->load_w = w;
    }

    r->seconds = bench_now_sec() - t0;
    return (r->have_single || r->have_all) ? 1 : 0;
}

/* --- output --------------------------------------------------------------- */

/* row -- the aligned two-column shape the whole report uses. */
#define BENCH_LABEL 16

static void row(Str *out, const char *label, const char *value) {
    size_t n = strlen(label);
    str_addz(out, "  ");
    str_addz(out, label);
    while (n < BENCH_LABEL) { str_addc(out, ' '); n++; }
    str_addz(out, value);
    str_addc(out, '\n');
}

/* fmt1 -- a double with one decimal, C89-safely (no snprintf). */
static void fmt1(Str *out, double v, const char *unit) {
    char buf[64];
    sprintf(buf, "%.1f", v);
    str_addz(out, buf);
    if (unit != NULL) { str_addc(out, ' '); str_addz(out, unit); }
}

static void fmt0(Str *out, double v, const char *unit) {
    char buf[64];
    sprintf(buf, "%.0f", v);
    str_addz(out, buf);
    if (unit != NULL) { str_addc(out, ' '); str_addz(out, unit); }
}

void bench_report(const BenchResult *r, Str *out) {
    Str v;
    str_init(&v);

    row(out, "cpu", r->cpu_model);

    str_reset(&v);
    str_addl(&v, r->ncpu);
    row(out, "threads", str_text(&v));

    if (r->have_single) {
        str_reset(&v);
        fmt0(&v, r->single_ops, "ops/s");
        row(out, "single-core", str_text(&v));
    }
    if (r->have_all) {
        str_reset(&v);
        fmt0(&v, r->all_ops, "ops/s");
        row(out, "all-core", str_text(&v));
    }

    if (r->have_power && r->load_w > 0.0) {
        str_reset(&v);
        fmt1(&v, r->load_w, "W under load, ");
        fmt1(&v, r->idle_w, "W idle");
        row(out, "package power", str_text(&v));

        /* ops per watt: the number that actually improves when an undervolt
         * works, which is why it is here and not behind a flag. */
        if (r->have_all) {
            str_reset(&v);
            fmt0(&v, r->all_ops / r->load_w, "ops/s per watt");
            row(out, "efficiency", str_text(&v));
        }
    } else {
        row(out, "package power", r->power_detail);
    }

    if (r->have_temp) {
        str_reset(&v);
        fmt0(&v, r->peak_temp_c, "C");
        row(out, "peak temp", str_text(&v));
    }
    if (r->have_freq) {
        str_reset(&v);
        str_addl(&v, r->peak_freq_khz / 1000);
        str_addz(&v, " MHz");
        row(out, "peak freq", str_text(&v));
    }

    str_reset(&v);
    fmt0(&v, r->seconds, "s");
    row(out, "duration", str_text(&v));

    str_free(&v);
}

/* json_str -- a JSON string literal with the escapes JSON actually requires. */
static void json_str(Str *out, const char *s) {
    str_addc(out, '"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') { str_addc(out, '\\'); str_addc(out, *s); }
        else if (*s == '\n') str_addz(out, "\\n");
        else if ((unsigned char)*s < 0x20) str_addc(out, ' ');
        else str_addc(out, *s);
    }
    str_addc(out, '"');
}

static void json_num(Str *out, const char *key, double v, int decimals, int *first) {
    char buf[64];
    if (!*first) str_addz(out, ",\n");
    *first = 0;
    str_addz(out, "  ");
    json_str(out, key);
    str_addz(out, ": ");
    sprintf(buf, decimals ? "%.2f" : "%.0f", v);
    str_addz(out, buf);
}

void bench_json(const BenchResult *r, Str *out) {
    int first = 1;

    str_addz(out, "{\n  ");
    json_str(out, "cpu");
    str_addz(out, ": ");
    json_str(out, r->cpu_model);
    first = 0;

    json_num(out, "threads", (double)r->ncpu, 0, &first);
    if (r->have_single) json_num(out, "single_ops", r->single_ops, 2, &first);
    if (r->have_all)    json_num(out, "all_ops", r->all_ops, 2, &first);
    if (r->have_power && r->load_w > 0.0) {
        json_num(out, "load_watts", r->load_w, 2, &first);
        json_num(out, "idle_watts", r->idle_w, 2, &first);
        if (r->have_all) json_num(out, "ops_per_watt", r->all_ops / r->load_w, 2, &first);
    }
    if (r->have_temp) json_num(out, "peak_temp_c", r->peak_temp_c, 1, &first);
    if (r->have_freq) json_num(out, "peak_freq_khz", (double)r->peak_freq_khz, 0, &first);
    json_num(out, "seconds", r->seconds, 1, &first);

    str_addz(out, ",\n  ");
    json_str(out, "power_source");
    str_addz(out, ": ");
    json_str(out, r->power_detail);
    str_addz(out, "\n}\n");
}
