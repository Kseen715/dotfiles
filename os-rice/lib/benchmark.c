/* lib/benchmark.c -- `osr benchmark cpu`: throughput, power and thermals, in
 * as few lines of output as the numbers allow.
 *
 * It is a command of its own rather than a flag on `osr undervolt` because
 * measuring a machine is useful without tuning it, and because the undervolt
 * perf gate needs exactly these numbers -- one implementation, consumed twice,
 * instead of two that drift apart.
 *
 * The default output is short on purpose. `--verbose` lets the workload's own
 * output through, `--json` emits the machine form, and neither is the default
 * because the common case is wanting to glance at the numbers.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "cmds.h"
#include "bench/bench.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* BENCH_EXIT_DEPS -- "the tools are not here". A distinct status rather than a
 * plain failure, because the front-end script acts on it: it installs the
 * module and runs us again. Installing packages needs the sh tier's module
 * machinery (and its privilege escalation), so the split is that this binary
 * knows WHAT is required and the script knows HOW to get it -- one source of
 * truth for the dependency list, no package management in here. */
#define BENCH_EXIT_DEPS 3

static int usage(void) {
    fputs("usage: osr benchmark cpu [options]\n", stderr);
    fputs("       osr benchmark sensors\n\n", stderr);
    fputs("  --seconds N     per-phase duration (default 20)\n", stderr);
    fputs("  --json          machine-readable output\n", stderr);
    fputs("  --verbose       let the workload's own output through\n", stderr);
    fputs("  --save NAME     store the result as a named baseline\n", stderr);
    fputs("  --compare NAME  print the delta against a stored baseline\n", stderr);
    fputs("  --install-deps  install the optional tools too, before running\n", stderr);
    fputs("\nMeasures single-core and all-core throughput, package power under\n", stderr);
    fputs("load and at idle, peak temperature and peak clock. Power needs a\n", stderr);
    fputs("RAPL, hwmon or battery sensor; without one the rest still works.\n", stderr);
    fputs("\nThe workload (stress-ng) is installed automatically when missing.\n", stderr);
    fputs("--install-deps additionally pulls in the optional cross-check tools\n", stderr);
    fputs("(lm_sensors, turbostat/perf), which nothing here calls.\n", stderr);
    fputs("\n`osr benchmark sensors` reports which power and temperature sources\n", stderr);
    fputs("this machine has, and what to do about the ones it does not.\n", stderr);
    return 2;
}

/* --- the baseline store --------------------------------------------------- */

/* Baselines live beside the undervolt journal, under $OSR_BENCH_DIR when set so
 * the tests never touch the real one. */
static void store_dir(Str *out) {
    str_addz(out, env_str("OSR_BENCH_DIR", "/var/lib/osr/bench"));
}

/* safe_name -- a baseline name is a filename, so anything that could climb out
 * of the directory is replaced rather than rejected: the name is a label, and
 * failing the whole run over a slash in it would be silly. */
static void safe_name(Str *out, const char *name) {
    for (; *name; name++) {
        char c = *name;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
              || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        str_addc(out, ok ? c : '-');
    }
}

static void store_path(Str *out, const char *name) {
    store_dir(out);
    str_addc(out, '/');
    safe_name(out, name);
    str_addz(out, ".json");
}

static int mkdir_parents(const char *path) {
    Str p;
    size_t i;
    int ok = 1;
    if (dir_exists(path)) return 1;
    str_init(&p);
    str_addz(&p, path);
    for (i = 1; i < p.len; i++) {
        if (p.p[i] != '/') continue;
        p.p[i] = '\0';
        if (!dir_exists(p.p) && mkdir(p.p, 0755) != 0 && !dir_exists(p.p)) { ok = 0; break; }
        p.p[i] = '/';
    }
    if (ok && !dir_exists(p.p) && mkdir(p.p, 0755) != 0 && !dir_exists(p.p)) ok = 0;
    str_free(&p);
    return ok;
}

static int save_baseline(const BenchResult *r, const char *name) {
    Str dir, path, json;
    FILE *f;
    int ok = 0;

    str_init(&dir);
    str_init(&path);
    str_init(&json);
    store_dir(&dir);
    store_path(&path, name);

    if (mkdir_parents(str_text(&dir))) {
        f = fopen(str_text(&path), "w");
        if (f != NULL) {
            bench_json(r, &json);
            ok = fwrite(json.p, 1, json.len, f) == json.len;
            if (fclose(f) != 0) ok = 0;
        }
    }
    if (ok) {
        Str msg;
        str_init(&msg);
        str_addz(&msg, "saved baseline to ");
        str_addz(&msg, str_text(&path));
        osr_info(str_text(&msg));
        str_free(&msg);
    } else {
        Str msg;
        str_init(&msg);
        str_addz(&msg, "could not write ");
        str_addz(&msg, str_text(&path));
        osr_warn(str_text(&msg));
        str_free(&msg);
    }
    str_free(&dir);
    str_free(&path);
    str_free(&json);
    return ok;
}

/* json_get_num -- one number out of a saved baseline.
 *
 * A hand-rolled scan rather than a JSON parser: this reads only files this
 * program wrote, the schema is flat, and pulling in a parser for six numbers
 * would be the larger risk. Returns 0 when the key is absent. */
static int json_get_num(const char *buf, size_t len, const char *key, double *out) {
    Str pat;
    const char *p, *end;
    size_t klen;
    int found = 0;

    str_init(&pat);
    str_addc(&pat, '"');
    str_addz(&pat, key);
    str_addz(&pat, "\":");
    klen = pat.len;

    end = buf + len;
    for (p = buf; p + klen <= end; p++) {
        if (memcmp(p, str_text(&pat), klen) != 0) continue;
        p += klen;
        while (p < end && is_space(*p)) p++;
        {
            char num[64];
            size_t n = 0;
            char *endp;
            while (p < end && n + 1 < sizeof(num)
                   && (*p == '-' || *p == '+' || *p == '.' || (*p >= '0' && *p <= '9'))) {
                num[n++] = *p++;
            }
            num[n] = '\0';
            if (n > 0) {
                *out = strtod(num, &endp);
                if (*endp == '\0') found = 1;
            }
        }
        break;
    }
    str_free(&pat);
    return found;
}

/* delta_row -- "all-core   31240 ops/s   (+3.2% vs stock)".
 *
 * The percentage is what the undervolt perf gate is really asking about, so the
 * comparison is expressed the same way here: relative to the baseline, signed. */
static void delta_row(Str *out, const char *label, int have_now, double now,
                      const char *base_buf, size_t base_len, const char *key,
                      const char *unit) {
    char buf[128];
    double was;
    size_t n;

    if (!have_now) return;
    n = strlen(label);
    str_addz(out, "  ");
    str_addz(out, label);
    while (n < 16) { str_addc(out, ' '); n++; }

    sprintf(buf, "%.0f", now);
    str_addz(out, buf);
    str_addc(out, ' ');
    str_addz(out, unit);

    if (json_get_num(base_buf, base_len, key, &was) && was != 0.0) {
        sprintf(buf, "   (%+.1f%% vs baseline)", (now - was) / was * 100.0);
        str_addz(out, buf);
    }
    str_addc(out, '\n');
}

static int compare_baseline(const BenchResult *r, const char *name) {
    Str path, out;
    char *buf;
    size_t len;

    str_init(&path);
    store_path(&path, name);
    buf = slurp(str_text(&path), &len);
    if (buf == NULL) {
        Str msg;
        str_init(&msg);
        str_addz(&msg, "no baseline at ");
        str_addz(&msg, str_text(&path));
        osr_warn(str_text(&msg));
        str_free(&msg);
        str_free(&path);
        return 0;
    }

    str_init(&out);
    str_addc(&out, '\n');
    delta_row(&out, "single-core", r->have_single, r->single_ops, buf, len, "single_ops", "ops/s");
    delta_row(&out, "all-core", r->have_all, r->all_ops, buf, len, "all_ops", "ops/s");
    if (r->have_power && r->load_w > 0.0) {
        delta_row(&out, "package power", 1, r->load_w, buf, len, "load_watts", "W");
        if (r->have_all) {
            delta_row(&out, "efficiency", 1, r->all_ops / r->load_w, buf, len,
                      "ops_per_watt", "ops/s per watt");
        }
    }
    out_flush(&out);
    str_free(&out);
    str_free(&path);
    free(buf);
    return 1;
}

/* --- the command ---------------------------------------------------------- */

static int cmd_cpu(int argc, char **argv) {
    BenchOpts opts;
    BenchResult r;
    Str out, missing;
    const char *save = NULL, *compare = NULL;
    int json = 0, i;

    bench_opts_init(&opts);

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            opts.verbose = 1;
        } else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            opts.seconds = (int)strtol(argv[++i], NULL, 10);
            if (opts.seconds < 1) {
                osr_error_line("--seconds must be at least 1");
                return 2;
            }
        } else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) {
            save = argv[++i];
        } else if (strcmp(argv[i], "--compare") == 0 && i + 1 < argc) {
            compare = argv[++i];
        } else if (strcmp(argv[i], "--install-deps") == 0) {
            /* Handled by the front-end script before we are reached: installing
             * packages is install.sh's job, not this binary's. Accepted here so
             * that calling build/osr directly is not a confusing error. */
            continue;
        } else {
            fprintf(stderr, "osr benchmark cpu: unknown option '%s'\n", argv[i]);
            return usage();
        }
    }

    str_init(&missing);
    if (bench_deps_missing(&missing) > 0) {
        /* Not an error line: through the `osr` front end this is about to be
         * fixed automatically, and shouting about a condition that resolves
         * itself two seconds later trains people to ignore the output. Someone
         * running build/osr directly still gets the name and the exit status. */
        Str msg;
        str_init(&msg);
        str_addz(&msg, "not installed: ");
        str_addz(&msg, str_text(&missing));
        osr_info(str_text(&msg));
        str_free(&msg);
        str_free(&missing);
        return BENCH_EXIT_DEPS;
    }
    str_free(&missing);

    /* --json is a machine format: its consumer parses stdout, so the phase
     * commentary would be noise at best. Everyone else gets it, because a run
     * is a minute of a loaded and otherwise silent machine. */
    if (!json) {
        Str msg;
        str_init(&msg);
        str_addz(&msg, "benchmarking (");
        str_addl(&msg, opts.seconds);
        str_addz(&msg, "s per load phase, this will load every core)");
        osr_info(str_text(&msg));
        str_free(&msg);
        opts.announce = 1;
    }

    if (!bench_cpu(&opts, &r)) {
        osr_error_line("benchmark produced no result - stress-ng did not run");
        return 1;
    }

    str_init(&out);
    if (json) {
        bench_json(&r, &out);
    } else {
        str_addc(&out, '\n');
        bench_report(&r, &out);
    }
    out_flush(&out);
    str_free(&out);

    if (compare != NULL) compare_baseline(&r, compare);
    if (save != NULL) save_baseline(&r, save);
    return 0;
}

/* cmd_sensors -- the working behind "no power sensor".
 *
 * Deliberately not a flag on `cpu`: it answers a question you ask INSTEAD of
 * benchmarking, and making it a flag would mean either loading the machine for
 * a minute first or having a flag that silently skips the thing the command is
 * named after. */
static int cmd_sensors(void) {
    Str out;
    str_init(&out);
    bench_sensors_report(&out);
    out_flush(&out);
    str_free(&out);
    return 0;
}

int osr_benchmark_main(int argc, char **argv) {
    if (argc < 2) return usage();
    if (strcmp(argv[1], "cpu") == 0) return cmd_cpu(argc - 2, argv + 2);
    if (strcmp(argv[1], "sensors") == 0) return cmd_sensors();
    fprintf(stderr, "osr benchmark: unknown object '%s' (try: cpu, sensors)\n", argv[1]);
    return usage();
}
