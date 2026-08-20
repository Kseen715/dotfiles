/* lib/undervolt.c -- `osr undervolt`: CPU voltage offsets, by hand or found
 * automatically.
 *
 * The loop this exists to automate:
 *
 *     undervolt -> stress -> crashed/errored ? back off : record, go deeper
 *               -> repeat -> long soak at the safe value
 *
 * Doing that by hand is an evening per machine and you get no record of where
 * you were when the box locked up. The pieces already exist (intel-undervolt,
 * ryzen_smu, stress-ng); what does not is anything that runs the loop, decides
 * what "stable" means, and survives the machine dying mid-test.
 *
 * The layering, and why it is split this way:
 *
 *   lib/uv/backend.h    four verbs per vendor. All the hardware knowledge, and
 *                       nothing else, lives behind it.
 *   lib/uv/ backends    amd_smu, intel_msr, arm_dt, generic_opp.
 *   lib/uv/journal.c    crash-safe record of what was applied when. The
 *                       machine locking up hard IS the expected failure, so
 *                       this is load-bearing, not bookkeeping.
 *   lib/uv/stress.c     the tiered validator. Instability shows up in
 *                       single-core boost and in result-VERIFYING workloads far
 *                       more than in generic all-core load, so "run stress-ng
 *                       for a while" is not the test.
 *   lib/uv/search.c     the vendor-neutral descent: coarse step, back off,
 *                       refine, subtract a safety margin, soak.
 *   this file           argument parsing and the reports. No hardware.
 *
 * `probe` is the verb everything else is built behind. It never mutates, it
 * works on any machine including one with no voltage control at all, and its
 * answer is what decides whether the rest of the command has anything to do.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "cmds.h"
#include "uv/backend.h"

/* not_yet -- the verbs whose implementation lands in a later step. They are
 * listed in the usage text and refuse loudly rather than being hidden, because
 * a command that silently lacks `reset` is worse than one that admits it. */
static int not_yet(const char *verb, const char *step) {
    Str s;
    str_init(&s);
    str_addz(&s, "undervolt cpu ");
    str_addz(&s, verb);
    str_addz(&s, ": not implemented yet (");
    str_addz(&s, step);
    str_addz(&s, ")");
    osr_error_line(str_text(&s));
    str_free(&s);
    return 1;
}

static int usage(void) {
    fputs("usage: osr undervolt cpu <verb> [options]\n\n", stderr);
    fputs("  probe          what this machine exposes; never writes anything\n", stderr);
    fputs("  status         current offsets, saved profile, journal tail\n", stderr);
    fputs("  set            apply offsets by hand (--core/--cache/--gpu/--uncore)\n", stderr);
    fputs("  reset          everything back to stock\n", stderr);
    fputs("  test           validate whatever is applied right now\n", stderr);
    fputs("  auto           find the deepest stable undervolt, then soak it\n", stderr);
    fputs("  resume         continue a search interrupted by a crash or a reboot\n", stderr);
    fputs("  apply          re-apply the validated profile\n", stderr);
    fputs("  enable-boot    apply the validated profile at every boot (opt-in)\n", stderr);
    fputs("  disable-boot   stop doing that\n", stderr);
    fputs("\nStart with `osr undervolt cpu probe`: on most machines the answer is\n", stderr);
    fputs("that firmware has voltage control locked, and that is worth knowing\n", stderr);
    fputs("before anything else.\n", stderr);
    return 2;
}

/* cmd_probe -- the capability report.
 *
 * Exit status is the machine-readable half: 0 when something is tunable, 1
 * when nothing is. That lets a script (or `auto`, or a module) gate on it
 * without parsing the text, and it is why a VM returning "nothing here" is a
 * non-zero exit rather than an error message.
 */
static int cmd_probe(void) {
    const UvBackend *be;
    UvCaps caps;
    Str report, line;
    int d, tunable;

    str_init(&report);
    str_init(&line);

    be = uv_detect(&caps, &report);

    str_addz(&line, "backend: ");
    str_addz(&line, be->name);
    osr_info(str_text(&line));

    fputs(str_text(&report), stdout);

    /* The domain table only earns its space when some plane exists; the
     * generic backend has none and has already explained why. */
    tunable = uv_caps_any_writable(&caps);
    if (tunable) {
        fputs("\n  plane      present  readable  writable  units\n", stdout);
        for (d = 0; d < UV_DOMAIN_MAX; d++) {
            if (!caps.present[d]) continue;
            printf("  %-9s  %-7s  %-8s  %-8s  %d\n",
                   uv_domain_name((UvDomain)d),
                   "yes",
                   caps.readable[d] ? "yes" : "no",
                   caps.writable[d] ? "yes" : "no",
                   caps.count[d]);
        }
        printf("\n  offset range %d..%d mV, search step %d mV\n",
               caps.min_mv, caps.max_mv, caps.step_mv);
        if (!caps.settings_volatile) {
            fputs("\n", stdout);
            osr_warn("offsets on this backend SURVIVE a power cycle: a bad value "
                     "boot-loops the board rather than clearing itself");
        }
        if (caps.needs_reboot) {
            osr_warn("each change needs a reboot to take effect - a full search "
                     "here is hours, not minutes");
        }
    }

    fputs("\n", stdout);
    fflush(stdout);

    str_free(&report);
    str_free(&line);
    return tunable ? 0 : 1;
}

/* cmd_cpu -- the `cpu` object's verbs. The object word is there because
 * voltage is not the only thing worth tuning on a machine and `osr undervolt
 * gpu` should be able to exist later without rewriting this dispatch. */
static int cmd_cpu(int argc, char **argv) {
    const char *verb;

    if (argc < 1) return usage();
    verb = argv[0];

    if (strcmp(verb, "probe") == 0) return cmd_probe();

    if (strcmp(verb, "status") == 0)       return not_yet(verb, "step 3");
    if (strcmp(verb, "set") == 0)          return not_yet(verb, "step 3");
    if (strcmp(verb, "reset") == 0)        return not_yet(verb, "step 3");
    if (strcmp(verb, "test") == 0)         return not_yet(verb, "step 4");
    if (strcmp(verb, "auto") == 0)         return not_yet(verb, "step 5");
    if (strcmp(verb, "resume") == 0)       return not_yet(verb, "step 5");
    if (strcmp(verb, "apply") == 0)        return not_yet(verb, "step 6");
    if (strcmp(verb, "enable-boot") == 0)  return not_yet(verb, "step 6");
    if (strcmp(verb, "disable-boot") == 0) return not_yet(verb, "step 6");

    fprintf(stderr, "osr undervolt cpu: unknown verb '%s'\n", verb);
    return usage();
}

int osr_undervolt_main(int argc, char **argv) {
    if (argc < 2) return usage();
    if (strcmp(argv[1], "cpu") == 0) return cmd_cpu(argc - 2, argv + 2);

    fprintf(stderr, "osr undervolt: unknown object '%s' (only 'cpu' so far)\n", argv[1]);
    return usage();
}
