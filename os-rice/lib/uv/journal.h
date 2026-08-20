/* lib/uv/journal.h -- the crash-safe record of what was applied, and when.
 *
 * Every other part of this feature assumes the machine survives long enough to
 * report a result. This part assumes it does not. An undervolt that has gone
 * one step too far does not return an error -- it hard-locks the box between
 * one instruction and the next, and the next thing that runs is the BIOS. So
 * the search's memory cannot live in the search's process.
 *
 * The protocol is the whole idea, and it is three lines long:
 *
 *     append TRY <what we are about to apply>   <- fsync, file AND directory
 *     apply it
 *     run the test
 *     append OK (or FAIL) <the same thing>      <- fsync
 *
 * A TRY with no verdict after it is therefore a machine that died mid-test.
 * Distinguishing "died" from "still running" is what the boot id is for: every
 * record carries the boot it was written in, so a dangling TRY from a PREVIOUS
 * boot is a crash, while a dangling TRY from THIS boot is just an interrupted
 * run (someone hit ^C, the process was killed). Those two need opposite
 * responses -- back off versus carry on -- and nothing else in the system can
 * tell them apart.
 *
 * That is also why the fsync is of the directory as well as the file: a
 * freshly created journal whose directory entry never reached the platter
 * reads as "no journal at all" after the power cycle, which is precisely the
 * case we are trying to survive.
 *
 * The analysis is deliberately a pure function over a record array
 * (uv_journal_analyze), separate from the file I/O, because the interesting
 * behaviour here is exactly the behaviour that is impossible to provoke on
 * purpose: three crashes at the same offset, a stale boot id, a journal that
 * ends mid-line. Those get tested from synthetic records instead of from a
 * machine we keep switching off.
 *
 * C89 + POSIX.
 */
#ifndef OSR_UV_JOURNAL_H
#define OSR_UV_JOURNAL_H

#include "backend.h"

/* --- records -------------------------------------------------------------- */

typedef enum {
    UV_J_TRY = 0,  /* about to apply this; nothing is known yet */
    UV_J_OK,       /* applied, tested, passed */
    UV_J_FAIL,     /* applied, machine survived, test said no */
    UV_J_CRASH,    /* synthesized on the next boot for a TRY that never resolved */
    UV_J_RESET,    /* everything put back to stock */
    UV_J_KIND_MAX
} UvJKind;

const char *uv_jkind_name(UvJKind k);
UvJKind uv_jkind_parse(const char *s);

#define UV_J_STRMAX 48

typedef struct {
    UvJKind kind;
    char backend[UV_J_STRMAX];
    UvDomain domain;
    int idx;                     /* core index, or 0 for whole-domain planes */
    int mv;
    char phase[UV_J_STRMAX];     /* "coarse", "refine", "soak", ... */
    char boot_id[UV_J_STRMAX];
    long ts;                     /* time(NULL) */
} UvJRec;

/* uv_jrec_init -- a zeroed record with sane strings, so a caller only fills in
 * what it means. */
void uv_jrec_init(UvJRec *r);
/* uv_jrec_same_step -- same domain, unit and offset. What "we have been here
 * before" means, and therefore what the per-step crash brake counts. */
int uv_jrec_same_step(const UvJRec *a, const UvJRec *b);

/* uv_jrec_format -- one line, no trailing newline. Fields are space-separated
 * and any whitespace inside a string field is replaced, because a record that
 * cannot be read back is worse than one that lost a character.
 * uv_jrec_parse returns 0 on a line that is not a record (a truncated tail
 * after a power cut, most obviously), which callers skip rather than treat as
 * an error. */
void uv_jrec_format(Str *out, const UvJRec *r);
int uv_jrec_parse(const char *line, size_t len, UvJRec *out);

/* --- the file ------------------------------------------------------------- */

/* uv_journal_dir / uv_journal_path -- /var/lib/osr/undervolt[/journal],
 * or $OSR_UV_DIR when set. The override exists so the tests never touch the
 * real one; it is the same trick lib/state.c plays with $OSR_HOME. */
void uv_journal_dir(Str *out);
void uv_journal_path(Str *out);

/* uv_boot_id -- this boot's id from /proc/sys/kernel/random/boot_id, or
 * $OSR_UV_BOOT_ID when set (tests again). Falls back to "unknown" on a kernel
 * that does not have it -- which degrades crash detection to "assume the worst
 * and treat a dangling TRY as a crash", the safe direction. */
void uv_boot_id(Str *out);

/* uv_journal_append -- one record, durably. Creates the directory if needed,
 * then fsyncs the file and its directory before returning. This is the call
 * that must complete BEFORE the voltage write it describes. Returns UV_OK/UV_ERR. */
int uv_journal_append(const UvJRec *r);

/* uv_journal_load -- the whole journal as an array the caller frees. Returns
 * UV_OK even for a missing journal (*n == 0): a first run has no history, and
 * that is not a failure. Unparseable lines are skipped. */
int uv_journal_load(UvJRec **recs, size_t *n);

/* --- recovery ------------------------------------------------------------- */

/* The brakes. A search that keeps crashing is not converging on an answer, it
 * is repeatedly breaking the machine, and at some point the right move is to
 * stop and let a person look. */
#define UV_BRAKE_SAME_STEP 3
#define UV_BRAKE_TOTAL     5

typedef struct {
    /* The last TRY dangles and was written in an earlier boot: the machine
     * went down with `crash_rec` applied. */
    int crashed;
    UvJRec crash_rec;

    /* The last TRY dangles but is from THIS boot: an interrupted run, not a
     * crash. The offset may still be applied right now. */
    int interrupted;
    UvJRec pending_rec;

    /* The deepest setting that ever passed. Where a resumed search restarts. */
    int have_last_good;
    UvJRec last_good;

    int crashes_total;
    int crashes_at_step;    /* crashes matching crash_rec's step, this one included */

    /* Stop. Either brake tripped, and no further automatic descent may happen
     * until a person clears the journal. */
    int brake;
    const char *brake_reason;
} UvRecovery;

/* uv_journal_analyze -- the pure core: what does this history mean, given the
 * boot we are in now? No I/O, so the tests can hand it any history at all. */
void uv_journal_analyze(const UvJRec *recs, size_t n,
                        const char *cur_boot_id, UvRecovery *out);

/* uv_journal_recover -- uv_journal_load + uv_journal_analyze against the real
 * boot id. Returns UV_OK unless the journal could not be read. */
int uv_journal_recover(UvRecovery *out);

/* uv_journal_note_crash -- write the CRASH record that uv_journal_analyze
 * inferred, so the next run does not have to infer it again and the crash
 * counters keep working. Call once, after a recover() that reported crashed. */
int uv_journal_note_crash(const UvRecovery *rec);

#endif /* OSR_UV_JOURNAL_H */
