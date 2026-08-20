/* test/unit_c/uv_journal_test.c -- the undervolt journal's crash-recovery
 * state machine.
 *
 * This is the test that matters most in the whole feature, because the
 * behaviour it covers is the behaviour nobody can reproduce on demand: the
 * machine hard-locking mid-write, three times at the same offset, or the power
 * going out halfway through appending a record. uv_journal_analyze is a pure
 * function over a record array precisely so that all of that can be handed to
 * it as data.
 *
 * The round-trip and durability halves are checked too, against a journal in a
 * temp directory ($OSR_UV_DIR), so nothing here touches /var/lib.
 */
/* Before any include: the test itself uses mkdtemp/setenv, and c_test.h pulls
 * in <stdio.h> first, which locks the feature-test macros in. The .c files
 * below define this too, but by then it is too late to matter. */
#define _POSIX_C_SOURCE 200809L

#include "../c_test.h"

#include "../../lib/uv/journal.c"
#include "../../lib/uv/backend.c"
#include "../../lib/uv/generic_opp.c"
#include "../../lib/common.c"

#include <stdlib.h>
#include <unistd.h>

#define BOOT_NOW  "boot-current"
#define BOOT_PREV "boot-previous"

/* mk -- a record, briefly. */
static UvJRec mk(UvJKind kind, int idx, int mv, const char *boot) {
    UvJRec r;
    uv_jrec_init(&r);
    r.kind = kind;
    r.domain = UV_CORE;
    r.idx = idx;
    r.mv = mv;
    strcpy(r.backend, "amd-smu");
    strcpy(r.phase, "coarse");
    strncpy(r.boot_id, boot, sizeof(r.boot_id) - 1);
    r.ts = 1700000000L;
    return r;
}

/* --- serialisation -------------------------------------------------------- */

static void test_roundtrip(void) {
    UvJRec in, out;
    Str line;

    in = mk(UV_J_TRY, 3, -35, BOOT_NOW);
    str_init(&line);
    uv_jrec_format(&line, &in);

    osr_t_eq_str("format is the documented line shape",
                 str_text(&line),
                 "TRY amd-smu core 3 -35 coarse boot-current 1700000000");

    osr_t_true("parses back", uv_jrec_parse(str_text(&line), line.len, &out));
    osr_t_eq_int("kind survives", out.kind, UV_J_TRY);
    osr_t_eq_int("domain survives", out.domain, UV_CORE);
    osr_t_eq_int("index survives", out.idx, 3);
    osr_t_eq_int("negative offset survives", out.mv, -35);
    osr_t_eq_str("backend survives", out.backend, "amd-smu");
    osr_t_eq_str("boot id survives", out.boot_id, "boot-current");
    osr_t_eq_int("timestamp survives", out.ts, 1700000000L);

    str_free(&line);
}

/* A journal's last line is the one most likely to be torn in half, because it
 * is the one being written when the power goes. It must be rejected, not
 * half-believed. */
static void test_truncated_line_rejected(void) {
    UvJRec out;
    const char *torn = "TRY amd-smu core 3 -35 coa";
    osr_t_true("a truncated record is refused",
               !uv_jrec_parse(torn, strlen(torn), &out));

    osr_t_true("an empty line is refused", !uv_jrec_parse("", 0, &out));
    osr_t_true("junk is refused", !uv_jrec_parse("hello", 5, &out));
    /* A plausible-looking line with an unknown domain must not silently become
     * core: that would attribute a crash to the wrong plane. */
    osr_t_true("an unknown domain is refused",
               !uv_jrec_parse("TRY amd-smu bogus 0 -10 coarse b 1", 34, &out));
}

/* --- analysis ------------------------------------------------------------- */

static void test_clean_history(void) {
    UvJRec recs[4];
    UvRecovery rec;

    recs[0] = mk(UV_J_TRY, 0, -10, BOOT_NOW);
    recs[1] = mk(UV_J_OK,  0, -10, BOOT_NOW);
    recs[2] = mk(UV_J_TRY, 0, -20, BOOT_NOW);
    recs[3] = mk(UV_J_OK,  0, -20, BOOT_NOW);

    uv_journal_analyze(recs, 4, BOOT_NOW, &rec);
    osr_t_true("clean run: no crash", !rec.crashed);
    osr_t_true("clean run: not interrupted", !rec.interrupted);
    osr_t_true("clean run: has a last-good", rec.have_last_good);
    osr_t_eq_int("clean run: last-good is the deepest pass", rec.last_good.mv, -20);
    osr_t_eq_int("clean run: no crashes counted", rec.crashes_total, 0);
    osr_t_true("clean run: brake off", !rec.brake);
}

static void test_empty_history(void) {
    UvRecovery rec;
    uv_journal_analyze(NULL, 0, BOOT_NOW, &rec);
    osr_t_true("first run: no crash", !rec.crashed);
    osr_t_true("first run: no last-good", !rec.have_last_good);
    osr_t_true("first run: brake off", !rec.brake);
}

/* The core case. A TRY with no verdict, written in a PREVIOUS boot, is a
 * machine that hard-locked with that offset applied. */
static void test_dangling_try_from_previous_boot_is_a_crash(void) {
    UvJRec recs[3];
    UvRecovery rec;

    recs[0] = mk(UV_J_TRY, 0, -10, BOOT_PREV);
    recs[1] = mk(UV_J_OK,  0, -10, BOOT_PREV);
    recs[2] = mk(UV_J_TRY, 0, -20, BOOT_PREV);   /* died here */

    uv_journal_analyze(recs, 3, BOOT_NOW, &rec);
    osr_t_true("stale dangling TRY is a crash", rec.crashed);
    osr_t_eq_int("the crash names the offset that did it", rec.crash_rec.mv, -20);
    osr_t_true("and we still know the last good one", rec.have_last_good);
    osr_t_eq_int("which is the step before", rec.last_good.mv, -10);
    osr_t_eq_int("counted once", rec.crashes_total, 1);
    osr_t_true("one crash does not trip the brake", !rec.brake);
}

/* The same dangling TRY, but from THIS boot, means the process was killed --
 * the machine is fine and the offset may still be live. Opposite response. */
static void test_dangling_try_from_this_boot_is_an_interruption(void) {
    UvJRec recs[2];
    UvRecovery rec;

    recs[0] = mk(UV_J_OK,  0, -10, BOOT_NOW);
    recs[1] = mk(UV_J_TRY, 0, -20, BOOT_NOW);

    uv_journal_analyze(recs, 2, BOOT_NOW, &rec);
    osr_t_true("same-boot dangling TRY is NOT a crash", !rec.crashed);
    osr_t_true("it is an interruption", rec.interrupted);
    osr_t_eq_int("and names what may still be applied", rec.pending_rec.mv, -20);
    osr_t_eq_int("nothing is counted as a crash", rec.crashes_total, 0);
}

/* An unreadable boot id degrades to "assume crash", never to "assume fine". */
static void test_unknown_boot_id_assumes_the_worst(void) {
    UvJRec recs[1];
    UvRecovery rec;
    recs[0] = mk(UV_J_TRY, 0, -20, BOOT_PREV);
    uv_journal_analyze(recs, 1, "unknown", &rec);
    osr_t_true("an unknown boot id treats a dangling TRY as a crash", rec.crashed);
}

/* A FAIL closes the TRY: the test said no, but the machine survived to say so.
 * That must not be counted as a crash or the brakes trip on ordinary results. */
static void test_fail_is_not_a_crash(void) {
    UvJRec recs[2];
    UvRecovery rec;
    recs[0] = mk(UV_J_TRY,  0, -60, BOOT_PREV);
    recs[1] = mk(UV_J_FAIL, 0, -60, BOOT_PREV);
    uv_journal_analyze(recs, 2, BOOT_NOW, &rec);
    osr_t_true("a resolved FAIL leaves no crash", !rec.crashed);
    osr_t_eq_int("and counts as no crash", rec.crashes_total, 0);
}

/* Brake 1: the same offset has taken the machine down UV_BRAKE_SAME_STEP
 * times. Two recorded crashes plus the one we are inferring now. */
static void test_same_step_brake(void) {
    UvJRec recs[5];
    UvRecovery rec;

    recs[0] = mk(UV_J_TRY,   0, -50, BOOT_PREV);
    recs[1] = mk(UV_J_CRASH, 0, -50, BOOT_PREV);
    recs[2] = mk(UV_J_TRY,   0, -50, BOOT_PREV);
    recs[3] = mk(UV_J_CRASH, 0, -50, BOOT_PREV);
    recs[4] = mk(UV_J_TRY,   0, -50, BOOT_PREV);   /* died a third time */

    uv_journal_analyze(recs, 5, BOOT_NOW, &rec);
    osr_t_eq_int("three strikes at one offset", rec.crashes_at_step, UV_BRAKE_SAME_STEP);
    osr_t_true("trips the brake", rec.brake);
    osr_t_true("with a reason", rec.brake_reason != NULL);
}

/* Crashes at DIFFERENT offsets must not trip the same-step brake -- descending
 * past several bad steps is what a search legitimately does. */
static void test_different_steps_do_not_trip_same_step_brake(void) {
    UvJRec recs[3];
    UvRecovery rec;

    recs[0] = mk(UV_J_CRASH, 0, -50, BOOT_PREV);
    recs[1] = mk(UV_J_CRASH, 0, -55, BOOT_PREV);
    recs[2] = mk(UV_J_TRY,   0, -60, BOOT_PREV);

    uv_journal_analyze(recs, 3, BOOT_NOW, &rec);
    osr_t_eq_int("only this offset's own crashes count", rec.crashes_at_step, 1);
    osr_t_eq_int("but all of them count toward the total", rec.crashes_total, 3);
    osr_t_true("three scattered crashes do not trip either brake", !rec.brake);
}

/* Brake 2: enough total damage, wherever it happened. */
static void test_total_brake(void) {
    UvJRec recs[UV_BRAKE_TOTAL];
    UvRecovery rec;
    int i;
    for (i = 0; i < UV_BRAKE_TOTAL; i++) {
        recs[i] = mk(UV_J_CRASH, 0, -50 - i, BOOT_PREV);
    }
    uv_journal_analyze(recs, (size_t)UV_BRAKE_TOTAL, BOOT_NOW, &rec);
    osr_t_eq_int("all crashes counted", rec.crashes_total, UV_BRAKE_TOTAL);
    osr_t_true("the total brake trips", rec.brake);
}

/* A RESET means nothing is applied -- but it must not launder the crash
 * history, or `reset` becomes a way to talk the brakes out of stopping. */
static void test_reset_clears_pending_but_not_crash_history(void) {
    UvJRec recs[6];
    UvRecovery rec;
    int i;
    for (i = 0; i < 5; i++) recs[i] = mk(UV_J_CRASH, 0, -50 - i, BOOT_PREV);
    recs[5] = mk(UV_J_RESET, 0, 0, BOOT_NOW);

    uv_journal_analyze(recs, 6, BOOT_NOW, &rec);
    osr_t_true("after a reset nothing is pending", !rec.crashed && !rec.interrupted);
    osr_t_eq_int("but the crashes are still on the record", rec.crashes_total, 5);
    osr_t_true("so the brake stays on", rec.brake);
}

/* Only the most recent TRY can be the one that killed the machine. */
static void test_latest_try_supersedes(void) {
    UvJRec recs[2];
    UvRecovery rec;
    recs[0] = mk(UV_J_TRY, 0, -30, BOOT_PREV);
    recs[1] = mk(UV_J_TRY, 0, -40, BOOT_PREV);
    uv_journal_analyze(recs, 2, BOOT_NOW, &rec);
    osr_t_true("still one crash", rec.crashed);
    osr_t_eq_int("attributed to the newest TRY", rec.crash_rec.mv, -40);
    osr_t_eq_int("counted once, not twice", rec.crashes_total, 1);
}

/* --- the file ------------------------------------------------------------- */

static void test_append_and_load(void) {
    char tmpl[] = "/tmp/osr-uv-jtest-XXXXXX";
    char *dir;
    UvJRec a, b;
    UvJRec *loaded = NULL;
    size_t n = 0;
    Str path;

    dir = mkdtemp(tmpl);
    if (dir == NULL) {
        osr_t_fail_msg("temp dir for journal I/O", "mkdtemp failed");
        return;
    }
    setenv("OSR_UV_DIR", dir, 1);
    setenv("OSR_UV_BOOT_ID", BOOT_NOW, 1);

    /* A missing journal is an empty history, not an error -- the very first
     * run has to work. */
    osr_t_eq_int("load of a missing journal succeeds", uv_journal_load(&loaded, &n), UV_OK);
    osr_t_eq_int("...with no records", (long)n, 0);
    free(loaded);
    loaded = NULL;

    a = mk(UV_J_TRY, 1, -25, BOOT_NOW);
    b = mk(UV_J_OK,  1, -25, BOOT_NOW);
    osr_t_eq_int("append TRY", uv_journal_append(&a), UV_OK);
    osr_t_eq_int("append OK", uv_journal_append(&b), UV_OK);

    osr_t_eq_int("reload", uv_journal_load(&loaded, &n), UV_OK);
    osr_t_eq_int("both records came back", (long)n, 2);
    if (n == 2) {
        osr_t_eq_int("first is the TRY", loaded[0].kind, UV_J_TRY);
        osr_t_eq_int("second is the OK", loaded[1].kind, UV_J_OK);
        osr_t_eq_int("offset survived the round trip", loaded[1].mv, -25);
    }
    free(loaded);

    /* The directory is created on demand: a fresh machine has no
     * /var/lib/osr/undervolt and the first append must not fail on that. */
    str_init(&path);
    uv_journal_path(&path);
    osr_t_true("journal file exists where the path says", file_exists(str_text(&path)));

    unlink(str_text(&path));
    rmdir(dir);
    str_free(&path);
    unsetenv("OSR_UV_DIR");
    unsetenv("OSR_UV_BOOT_ID");
}

/* A journal whose final line was torn in half by the power cut must still load
 * every complete record before it. */
static void test_torn_tail_still_loads_the_rest(void) {
    char tmpl[] = "/tmp/osr-uv-jtorn-XXXXXX";
    char *dir;
    Str path;
    FILE *f;
    UvJRec *loaded = NULL;
    size_t n = 0;

    dir = mkdtemp(tmpl);
    if (dir == NULL) {
        osr_t_fail_msg("temp dir for torn journal", "mkdtemp failed");
        return;
    }
    setenv("OSR_UV_DIR", dir, 1);

    str_init(&path);
    uv_journal_path(&path);
    f = fopen(str_text(&path), "w");
    if (f != NULL) {
        fputs("TRY amd-smu core 0 -10 coarse boot-previous 1700000000\n", f);
        fputs("OK amd-smu core 0 -10 coarse boot-previous 1700000001\n", f);
        fputs("TRY amd-smu core 0 -20 coa", f);   /* power went out here */
        fclose(f);
    }

    osr_t_eq_int("a torn journal still loads", uv_journal_load(&loaded, &n), UV_OK);
    osr_t_eq_int("keeping the complete records", (long)n, 2);
    if (n == 2) osr_t_eq_int("and dropping the torn one", loaded[1].kind, UV_J_OK);
    free(loaded);

    unlink(str_text(&path));
    rmdir(dir);
    str_free(&path);
    unsetenv("OSR_UV_DIR");
}

int main(void) {
    OSR_T_INIT();

    test_roundtrip();
    test_truncated_line_rejected();

    test_empty_history();
    test_clean_history();
    test_dangling_try_from_previous_boot_is_a_crash();
    test_dangling_try_from_this_boot_is_an_interruption();
    test_unknown_boot_id_assumes_the_worst();
    test_fail_is_not_a_crash();
    test_same_step_brake();
    test_different_steps_do_not_trip_same_step_brake();
    test_total_brake();
    test_reset_clears_pending_but_not_crash_history();
    test_latest_try_supersedes();

    test_append_and_load();
    test_torn_tail_still_loads_the_rest();

    return osr_t_finish();
}
