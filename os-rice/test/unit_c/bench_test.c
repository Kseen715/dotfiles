/* test/unit_c/bench_test.c -- the parts of `osr benchmark cpu` that can be
 * checked without heating a CPU up.
 *
 * Which is most of the ones that can be wrong. Running stress-ng and reading a
 * real power sensor is I/O; the places a bug actually hides are the stress-ng
 * report parser, the wrapped-energy-counter arithmetic, and the two output
 * formats. All three are pure functions for exactly this reason.
 *
 * The wrap case deserves its own note: RAPL counters roll over every few
 * minutes under load, which is to say precisely when a benchmark is running,
 * and a naive subtraction turns that into a huge negative number at the worst
 * possible moment.
 */
/* Before any include: c_test.h pulls in <stdio.h>, which locks the
 * feature-test macros in. See uv_journal_test.c. */
#define _POSIX_C_SOURCE 200809L

#include "../c_test.h"

#include "../../lib/bench/cpu.c"
#include "../../lib/bench/power.c"
#include "../../lib/bench/util.c"
#include "../../lib/common.c"

#include <stdlib.h>

/* --- stress-ng YAML ------------------------------------------------------- */

/* The real shape of a --yaml report, trimmed. The key detail is that
 * `bogo-ops-per-second-usr-sys-time` sits right next to the one we want and
 * shares a 19-character prefix with it. */
static const char *const YAML_ONE =
"---\n"
"system-info:\n"
"      stress-ng-version: 0.15.06\n"
"      run-by: root\n"
"metrics:\n"
"      - stressor: cpu\n"
"        bogo-ops: 24680\n"
"        bogo-ops-per-second-usr-sys-time: 154.321000\n"
"        bogo-ops-per-second-real-time: 1234.560000\n"
"        wall-clock-time: 20.000000\n"
"...\n";

static void test_yaml_single_stressor(void) {
    double ops = -1.0;
    osr_t_true("a stress-ng yaml report parses",
               bench_parse_yaml_ops(YAML_ONE, strlen(YAML_ONE), &ops));
    osr_t_eq_int("the real-time figure is the one taken", (long)ops, 1234L);
}

/* Several stressors sum, because an all-core run can report per-stressor rows
 * and the total throughput is what we are after. */
static void test_yaml_sums_stressors(void) {
    static const char *const yaml =
        "metrics:\n"
        "      - stressor: cpu\n"
        "        bogo-ops-per-second-real-time: 1000.000000\n"
        "      - stressor: cpu\n"
        "        bogo-ops-per-second-real-time: 500.500000\n";
    double ops = -1.0;
    osr_t_true("multi-stressor yaml parses", bench_parse_yaml_ops(yaml, strlen(yaml), &ops));
    osr_t_eq_int("throughput is summed", (long)(ops * 10), 15005L);
}

/* The neighbouring key must not be mistaken for ours: usr-sys time is a much
 * smaller number and silently using it would understate the machine. */
static void test_yaml_ignores_usr_sys_key(void) {
    static const char *const yaml =
        "        bogo-ops-per-second-usr-sys-time: 154.321000\n";
    double ops = -1.0;
    osr_t_true("a report with only the usr-sys key yields nothing",
               !bench_parse_yaml_ops(yaml, strlen(yaml), &ops));
}

static void test_yaml_rejects_junk(void) {
    double ops = -1.0;
    osr_t_true("empty input yields nothing", !bench_parse_yaml_ops("", 0, &ops));
    osr_t_true("unrelated text yields nothing",
               !bench_parse_yaml_ops("hello\nworld\n", 12, &ops));

    /* stress-ng killed mid-write leaves the key with no value. */
    {
        static const char *const torn = "        bogo-ops-per-second-real-time:";
        osr_t_true("a valueless key yields nothing",
                   !bench_parse_yaml_ops(torn, strlen(torn), &ops));
    }
    /* A value that is not a number must not contribute a salvaged prefix. */
    {
        static const char *const bad =
            "        bogo-ops-per-second-real-time: 12ab34\n";
        osr_t_true("a malformed value yields nothing",
                   !bench_parse_yaml_ops(bad, strlen(bad), &ops));
    }
}

/* A good row alongside a bad one keeps the good one and drops the bad one
 * entirely -- no partial credit from strtod. */
static void test_yaml_bad_row_does_not_pollute_total(void) {
    static const char *const yaml =
        "        bogo-ops-per-second-real-time: 1000.000000\n"
        "        bogo-ops-per-second-real-time: 99qq\n";
    double ops = -1.0;
    osr_t_true("parses", bench_parse_yaml_ops(yaml, strlen(yaml), &ops));
    osr_t_eq_int("only the valid row counted", (long)ops, 1000L);
}

/* --- energy counter arithmetic -------------------------------------------- */

static void test_energy_delta(void) {
    osr_t_eq_int("ordinary forward delta",
                 (long)pwr_energy_delta(1000UL, 5000UL, 100000UL), 4000L);
    osr_t_eq_int("zero delta",
                 (long)pwr_energy_delta(5000UL, 5000UL, 100000UL), 0L);

    /* Wrapped, with the range the kernel gave us: 100000 -> 10 means it went
     * 100000..99999 then 0..10. */
    osr_t_eq_int("wrap with a known range",
                 (long)pwr_energy_delta(99000UL, 10UL, 100000UL), 1010L);

    /* Wrapped with no declared range: assume the counter is 32-bit. Getting
     * this merely approximately right beats reporting negative energy. */
    osr_t_eq_int("wrap with no known range",
                 (long)pwr_energy_delta(0xFFFFFFF0UL, 15UL, 0UL), 31L);
}

/* --- the report ----------------------------------------------------------- */

static BenchResult sample_result(void) {
    BenchResult r;
    bench_result_init(&r);
    strcpy(r.cpu_model, "AMD Ryzen 7 9800X3D 8-Core Processor");
    r.ncpu = 16;
    r.have_single = 1; r.single_ops = 4812.0;
    r.have_all = 1;    r.all_ops = 31240.0;
    r.have_power = 1;  r.load_w = 88.4; r.idle_w = 21.3;
    strcpy(r.power_detail, "RAPL package energy counter");
    r.have_temp = 1;   r.peak_temp_c = 78.0;
    r.have_freq = 1;   r.peak_freq_khz = 5240000L;
    r.seconds = 62.0;
    return r;
}

static void test_report_has_the_numbers(void) {
    BenchResult r = sample_result();
    Str out;
    str_init(&out);
    bench_report(&r, &out);

    osr_t_true("reports single-core", strstr(str_text(&out), "single-core") != NULL);
    osr_t_true("reports the single-core figure", strstr(str_text(&out), "4812") != NULL);
    osr_t_true("reports all-core", strstr(str_text(&out), "31240") != NULL);
    osr_t_true("reports load and idle power together",
               strstr(str_text(&out), "88.4 W under load, 21.3 W idle") != NULL);
    /* 31240 / 88.4 = 353.4 -> 353. The number the whole exercise is about. */
    osr_t_true("reports efficiency in ops per watt",
               strstr(str_text(&out), "353 ops/s per watt") != NULL);
    osr_t_true("reports peak temperature", strstr(str_text(&out), "78 C") != NULL);
    osr_t_true("reports peak clock in MHz", strstr(str_text(&out), "5240 MHz") != NULL);

    str_free(&out);
}

/* With no sensor the report must say why rather than print a zero, which would
 * read as "this CPU uses no power". */
static void test_report_without_power_explains(void) {
    BenchResult r = sample_result();
    Str out;
    r.have_power = 0;
    r.load_w = 0.0;
    strcpy(r.power_detail, "no power sensor: no RAPL, no hwmon power input, no battery");
    str_init(&out);
    bench_report(&r, &out);

    osr_t_true("says why power is missing",
               strstr(str_text(&out), "no power sensor") != NULL);
    osr_t_true("and prints no watt figure", strstr(str_text(&out), " W under load") == NULL);
    osr_t_true("and no efficiency figure", strstr(str_text(&out), "per watt") == NULL);
    osr_t_true("but still reports throughput", strstr(str_text(&out), "31240") != NULL);
    str_free(&out);
}

/* --- json ----------------------------------------------------------------- */

static void test_json_shape(void) {
    BenchResult r = sample_result();
    Str out;
    str_init(&out);
    bench_json(&r, &out);

    osr_t_true("opens an object", str_text(&out)[0] == '{');
    osr_t_true("closes it", strstr(str_text(&out), "\n}") != NULL);
    osr_t_true("carries the cpu name",
               strstr(str_text(&out), "\"cpu\": \"AMD Ryzen 7 9800X3D") != NULL);
    osr_t_true("carries all_ops", strstr(str_text(&out), "\"all_ops\"") != NULL);
    osr_t_true("carries ops_per_watt", strstr(str_text(&out), "\"ops_per_watt\"") != NULL);
    osr_t_true("names the power source", strstr(str_text(&out), "\"power_source\"") != NULL);
    /* No trailing comma before the close, or it is not JSON at all. */
    osr_t_true("no dangling comma", strstr(str_text(&out), ",\n}") == NULL);

    str_free(&out);
}

/* A CPU model string containing a quote would otherwise break the file that
 * --save writes and --compare reads. */
static void test_json_escapes(void) {
    BenchResult r = sample_result();
    Str out;
    strcpy(r.cpu_model, "Weird \"quoted\" CPU\\model");
    str_init(&out);
    bench_json(&r, &out);
    osr_t_true("quotes and backslashes are escaped",
               strstr(str_text(&out), "\\\"quoted\\\" CPU\\\\model") != NULL);
    str_free(&out);
}

/* Omitted metrics must be absent rather than present-and-zero: the perf gate
 * divides by these. */
static void test_json_omits_missing(void) {
    BenchResult r;
    Str out;
    bench_result_init(&r);
    r.ncpu = 4;
    str_init(&out);
    bench_json(&r, &out);
    osr_t_true("no single_ops key when unmeasured",
               strstr(str_text(&out), "single_ops") == NULL);
    osr_t_true("no load_watts key when unmeasured",
               strstr(str_text(&out), "load_watts") == NULL);
    osr_t_true("still valid-looking", strstr(str_text(&out), ",\n}") == NULL);
    str_free(&out);
}

/* --- power source detection ----------------------------------------------- */

/* Whatever this machine has, detection must terminate and leave an explanation
 * behind -- a blank `detail` is the one outcome the report cannot render. */
static void test_pwr_detect_always_explains(void) {
    PwrMeter m;
    int found = pwr_detect(&m);
    osr_t_true("detection leaves a human explanation", m.detail[0] != '\0');
    if (!found) {
        osr_t_eq_int("no source means PWR_NONE", m.source, PWR_NONE);
    } else {
        osr_t_true("a found source names a file it can read", m.path[0] != '\0');
    }
    osr_t_eq_str("source names are stable", pwr_source_name(PWR_RAPL), "rapl");
    osr_t_eq_str("...for every source", pwr_source_name(PWR_NONE), "none");
}

/* An instantaneous meter with no samples must report failure, not 0 W. */
static void test_pwr_end_without_samples_fails(void) {
    PwrMeter m;
    double w = -1.0;
    memset(&m, 0, sizeof(m));
    m.source = PWR_HWMON;
    pwr_begin(&m);
    osr_t_true("no samples means no reading", !pwr_end(&m, &w));
    osr_t_eq_int("and the output is untouched", (long)w, -1L);
}

/* --- the CPU name --------------------------------------------------------- */

/* squeezed -- str_add_squeezed as a plain string function, for readability. */
static const char *squeezed(Str *tmp, const char *in) {
    str_reset(tmp);
    str_add_squeezed(tmp, in, strlen(in));
    return str_text(tmp);
}

/* Intel's brand string is a fixed-width field, so the padding is part of the
 * name that /proc/cpuinfo and lscpu both hand over. Every line that prints the
 * model -- the detect summary, the benchmark report, a saved baseline -- shows
 * the gap unless it is closed here. */
static void test_cpu_name_squeeze(void) {
    Str t;
    str_init(&t);

    osr_t_eq_str("interior padding collapses to one space",
                 squeezed(&t, "Intel(R) Core(TM) i7-8550U  CPU @ 1.80GHz"),
                 "Intel(R) Core(TM) i7-8550U CPU @ 1.80GHz");
    osr_t_eq_str("tabs count as whitespace too",
                 squeezed(&t, "Intel(R) Xeon(R)		W-2295 CPU"),
                 "Intel(R) Xeon(R) W-2295 CPU");
    osr_t_eq_str("both ends are trimmed",
                 squeezed(&t, "   AMD Ryzen 7 9800X3D 8-Core Processor 	 "),
                 "AMD Ryzen 7 9800X3D 8-Core Processor");
    osr_t_eq_str("a name with no runs is untouched",
                 squeezed(&t, "AMD Ryzen 7 5800X 8-Core Processor"),
                 "AMD Ryzen 7 5800X 8-Core Processor");
    osr_t_eq_str("all-whitespace squeezes to nothing", squeezed(&t, " \t  "), "");
    osr_t_eq_str("so does empty", squeezed(&t, ""), "");

    str_free(&t);
}

int main(void) {
    OSR_T_INIT();

    test_cpu_name_squeeze();

    test_yaml_single_stressor();
    test_yaml_sums_stressors();
    test_yaml_ignores_usr_sys_key();
    test_yaml_rejects_junk();
    test_yaml_bad_row_does_not_pollute_total();

    test_energy_delta();

    test_report_has_the_numbers();
    test_report_without_power_explains();

    test_json_shape();
    test_json_escapes();
    test_json_omits_missing();

    test_pwr_detect_always_explains();
    test_pwr_end_without_samples_fails();

    return osr_t_finish();
}
