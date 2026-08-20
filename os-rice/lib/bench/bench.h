/* lib/bench/bench.h -- measuring a CPU: throughput, power, thermals, clocks.
 *
 * This exists as its own command (`osr benchmark cpu`) rather than as a private
 * corner of the undervolting code, because "what is this machine actually
 * doing" is a question worth asking on its own. The undervolt perf gate then
 * consumes these numbers instead of growing a second, subtly different
 * measurement of the same thing.
 *
 * The headline number for undervolting is not ops/s, it is ops per watt.
 * Throughput barely moves when an undervolt works -- what changes is the power
 * needed to reach it, and therefore how long the part can hold boost. So power
 * is a first-class measurement here, not an optional extra.
 *
 * Power has no portable interface on Linux, so it is layered exactly the way
 * lib/uv/backend.h layers voltage control: try each source, take the first that
 * answers, and if none does say so rather than invent a number. The sources
 * differ in kind, which the code has to respect:
 *
 *   RAPL      an ENERGY COUNTER in microjoules. Accurate, and the only one that
 *             cannot miss a spike between samples -- but it wraps, and it is
 *             root-only on most kernels since the PLATYPUS side channel.
 *   hwmon     an INSTANTANEOUS reading in microwatts. Has to be sampled
 *             repeatedly and averaged; whatever happens between samples is lost.
 *   SMU       package power straight from the Ryzen SMU's PM table. Best on
 *             Granite Ridge, and the driver is already a dependency there.
 *   battery   discharge rate. Laptops only, and only while on battery.
 *
 * That difference is why the workload runs as a child process while the parent
 * polls: an instantaneous source needs sampling throughout, and the polling
 * loop is also where peak temperature and peak clock come from.
 *
 * The parsing and arithmetic are deliberately pure functions
 * (bench_parse_yaml_ops, pwr_energy_delta) so the interesting edge cases -- a
 * wrapped energy counter, a truncated stress-ng report -- are unit-testable
 * without a CPU to heat up.
 *
 * C89 + POSIX.
 */
#ifndef OSR_BENCH_H
#define OSR_BENCH_H

#include "../common.h"

#define BENCH_PATH_MAX 256
#define BENCH_TEXT_MAX 160

/* --- shared helpers (lib/bench/util.c) ------------------------------------
 *
 * Both units read one-line sysfs attributes and compose paths; these are the
 * one copy. A value that exists but does not parse reads as absent, uniformly,
 * because the alternative is two files disagreeing about what a malformed
 * wattage means.
 */
int bench_read_trim(Str *out, const char *path);
int bench_read_long(const char *path, long *out);
int bench_read_ulong(const char *path, unsigned long *out);
void bench_join3(Str *out, const char *a, const char *b, const char *c);
void bench_set_str(char *dst, size_t cap, const char *src);
double bench_now_sec(void);

/* --- power ---------------------------------------------------------------- */

typedef enum {
    PWR_NONE = 0,
    PWR_RAPL,      /* energy counter, microjoules, wraps */
    PWR_HWMON,     /* instantaneous, microwatts */
    PWR_SMU,       /* ryzen_smu PM table, package watts */
    PWR_BATTERY,   /* discharge rate, microwatts */
    PWR_SOURCE_MAX
} PwrSource;

const char *pwr_source_name(PwrSource s);

typedef struct {
    PwrSource source;
    char path[BENCH_PATH_MAX];      /* the file actually sampled */
    char detail[BENCH_TEXT_MAX];    /* how it was found, or why it was not */

    unsigned long max_range_uj;     /* RAPL wrap point; 0 when unknown */

    /* instantaneous sources accumulate; energy counters use the endpoints */
    double sum_w;
    long samples;
    unsigned long e_start;
    double t_start;
} PwrMeter;

/* pwr_detect -- pick a source. Returns 1 when one was found; on 0 the meter's
 * `detail` explains what was tried, which is what the report prints. */
int pwr_detect(PwrMeter *m);
/* pwr_begin / pwr_sample / pwr_end -- bracket a measurement. pwr_sample is a
 * no-op for energy counters and the whole measurement for instantaneous ones,
 * so callers can poll unconditionally. */
void pwr_begin(PwrMeter *m);
void pwr_sample(PwrMeter *m);
int pwr_end(PwrMeter *m, double *watts);

/* pwr_energy_delta -- b - a for a counter that wraps at max_range.
 *
 * RAPL counters are 32-bit-ish and roll over every few minutes under load, so
 * a naive subtraction produces a huge negative jump exactly when the machine is
 * busiest. Pure, and tested. max_range of 0 means "assume no wrap". */
unsigned long pwr_energy_delta(unsigned long a, unsigned long b, unsigned long max_range);

/* --- results -------------------------------------------------------------- */

typedef struct {
    char cpu_model[BENCH_TEXT_MAX];
    int ncpu;

    int have_single;
    double single_ops;
    int have_all;
    double all_ops;

    int have_power;
    double load_w;
    double idle_w;
    char power_detail[BENCH_TEXT_MAX];

    int have_temp;
    double peak_temp_c;
    int have_freq;
    long peak_freq_khz;

    double seconds;
} BenchResult;

void bench_result_init(BenchResult *r);

/* bench_report -- the short human form: one aligned label/value per line and
 * nothing else. This is the default output because the whole point of the
 * command is to be callable when you just want the numbers. */
void bench_report(const BenchResult *r, Str *out);
/* bench_json -- the same data for machines, and the on-disk format that
 * --save/--compare and the undervolt perf gate use. */
void bench_json(const BenchResult *r, Str *out);

/* --- running -------------------------------------------------------------- */

typedef struct {
    int seconds;   /* per phase */
    int verbose;   /* let the workload's own output through */
    int announce;  /* name each phase as it starts */
} BenchOpts;

/* bench_deps_missing -- names of the tools that are not installed, appended to
 * out space-separated. Returns how many. */
int bench_deps_missing(Str *out);

/* bench_cpu -- idle power, then single-core, then all-core. With o->announce
 * each phase says what it is and how long it takes before it starts: the run is
 * a minute of a silent, fully loaded machine, and "which of these is it doing
 * now" is not answerable from a single line printed at the top.
 *
 * Returns 1 when at least the throughput phases ran; a machine with no power
 * sensor still produces a useful result, so a missing meter is not a failure. */
int bench_cpu(const BenchOpts *o, BenchResult *r);

/* bench_parse_yaml_ops -- pull the summed bogo-ops-per-second-real-time out of
 * a stress-ng --yaml report. Parsing YAML rather than the human metrics table
 * is deliberate: the table's column layout has changed between releases and
 * the key name has not. Returns 1 when at least one stressor was found. */
int bench_parse_yaml_ops(const char *buf, size_t len, double *ops);

#endif /* OSR_BENCH_H */
