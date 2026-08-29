/* test/unit_c/ui_test.c -- the live step window, the palette, and the step
 * counter.
 *
 * A rice install is minutes of package manager output, and the step window is
 * what turns that into something a person can watch: a few tail lines of the
 * running command, repainted in place, collapsed to one [ok] line when the
 * step finishes. Which makes this the unit whose bugs are the most VISIBLE and
 * the least dangerous -- with one exception, which is why the erase arithmetic
 * gets most of the assertions below.
 *
 * ERASING IS THE DANGEROUS PART. `done` moves the cursor up N rows and clears
 * them. If N is bigger than what was painted, it erases the user's scrollback
 * -- the output of whatever they ran before `osr install`. So the row count is
 * not a display detail: it is a number that must exactly equal what was
 * written, and `paint` returning it as an exit status is what makes that
 * checkable.
 *
 * THE OTHER RULE IS D-2: every byte is 7-bit ASCII. The spinner frames are
 * -\|/ rather than braille dots for the same reason the log tags are [ok]
 * rather than a check mark.
 *
 * Hermetic: the log files painted from are fixtures, and stdout is a pipe --
 * so the auto-degrade decision (SS3) reliably takes its non-TTY branch, which
 * is the branch a real install takes when piped to a file.
 *
 * Replaces test/unit/ui_c_parity.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

/* ui -- `osr ui <args>`; returns the exit status, which for `paint` is the
 * number of rows it wrote. */
static int ui1(const char *a) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "ui", a, (const char *)NULL);
}
static int ui3(const char *a, const char *b, const char *c, const char *d) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "ui", a, b, c, d, (const char *)NULL);
}

/* fx -- the absolute path of a fixture log. */
static const char *fx(const char *name) {
    static HStr p;
    static int ready = 0;
    HStr rel;
    if (!ready) { hs_init(&p); ready = 1; }
    hs_init(&rel);
    hs_add(&rel, "fx/");
    hs_add(&rel, name);
    hs_path(&p, hs_text(&sb.root), hs_text(&rel));
    hs_free(&rel);
    return hs_text(&p);
}

/* rows -- how many newline-terminated rows the last run wrote to stdout. */
static int rows(void) {
    const char *p = osr_sb_capture(&sb);
    int n = 0;
    for (; *p != '\0'; p++) {
        if (*p == '\n') n++;
    }
    return n;
}

/* longest -- the widest line written, in VISIBLE columns.
 *
 * Control bytes do not occupy a column, and the painter puts a `\r` and an
 * erase-line sequence in front of every row it writes -- counting those would
 * make every line look five columns wider than the terminal shows it. */
static int longest(void) {
    const char *p = osr_sb_capture(&sb);
    int best = 0, cur = 0;
    while (*p != '\0') {
        if (*p == '\n') { if (cur > best) best = cur; cur = 0; p++; continue; }
        if (*p == '\r') { p++; continue; }
        if (*p == '\033') {                    /* skip a CSI sequence entirely */
            p++;
            if (*p == '[') {
                p++;
                while (*p != '\0' && (*p < '@' || *p > '~')) p++;
                if (*p != '\0') p++;
            }
            continue;
        }
        cur++;
        p++;
    }
    if (cur > best) best = cur;
    return best;
}

static int all_ascii(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    for (; *p != '\0'; p++) {
        if (*p > 0x7f) return 0;
    }
    return 1;
}

int main(void) {
    osr_sb_init(&sb);

    /* One fixture per property of the pipeline the window is built from:
     * take the last N lines, strip carriage returns, strip ANSI, cut to the
     * terminal width. */
    osr_sb_write(&sb, "fx/plain.log", "a\nb\nc\nd\ne\nf\ng\n", 0644);
    osr_sb_write(&sb, "fx/crlf.log", "one\r\ntwo\r\n", 0644);
    osr_sb_write(&sb, "fx/ansi.log",
        "\033[32mgreen\033[0m\n\033[?25lhide\033[K\nplain\n", 0644);
    osr_sb_write(&sb, "fx/badesc.log",
        "esc \033[3 unterminated\nlone \033 esc\n", 0644);
    osr_sb_write(&sb, "fx/nonl.log", "no trailing newline", 0644);
    osr_sb_write(&sb, "fx/trailblank.log", "keep\n\n\n", 0644);
    osr_sb_write(&sb, "fx/blank.log", "\n\n\n", 0644);
    osr_sb_write(&sb, "fx/empty.log", "", 0644);
    {
        HStr big;
        int i;
        hs_init(&big);
        for (i = 0; i < 300; i++) hs_addc(&big, 'x');
        hs_addc(&big, '\n');
        osr_sb_write(&sb, "fx/long.log", hs_text(&big), 0644);
        hs_reset(&big);
        for (i = 1; i <= 5000; i++) { hs_addn(&big, (long)i); hs_addc(&big, '\n'); }
        osr_sb_write(&sb, "fx/big.log", hs_text(&big), 0644);
        hs_free(&big);
    }

    /* ================================================================
     * 1. The palette, and how it is decided
     *
     * `vars` prints the assignments a shell would eval. Decided ONCE here,
     * against the real terminal, and exported -- because a module runs as a
     * child whose stdout is the step log, so a per-process decision would come
     * out colourless in every module inside a coloured run.
     * ================================================================ */
    osr_sb_env(&sb, "NO_COLOR", "");
    ui1("vars");
    /* stdout is a pipe here, so there is no terminal to colour for. */
    osr_assert_out(&sb, "OSR_RED=''",
        "piped output gets no colour at all, whatever NO_COLOR says");
    osr_assert_out(&sb, "OSR_COLS=",
        "a column count is always published: the window has to fit something");

    osr_sb_env(&sb, "NO_COLOR", "1");
    ui1("vars");
    osr_assert_out(&sb, "OSR_RED=''", "NO_COLOR keeps the palette empty");

    /* ================================================================
     * 2. The step counter
     * ================================================================ */
    osr_sb_env(&sb, "OSR_STEP_N", "3");
    osr_sb_env(&sb, "OSR_STEP_TOTAL", "12");
    ui1("step-prefix");
    osr_assert_out_is(&sb, "[03/12] ",
        "the counter is zero-padded to the width of the total, and ends in a "
        "space so the caller can concatenate");

    osr_sb_env(&sb, "OSR_STEP_N", "10");
    osr_sb_env(&sb, "OSR_STEP_TOTAL", "100");
    ui1("step-prefix");
    osr_assert_out_is(&sb, "[10/100] ",
        "the padding is a MINIMUM of two digits, not the width of the total: "
        "a three-digit run is not re-padded, it just gets wider");

    osr_sb_env(&sb, "OSR_STEP_N", "0");
    osr_sb_env(&sb, "OSR_STEP_TOTAL", "0");
    ui1("step-prefix");
    osr_assert_out_is(&sb, "",
        "with no total known the prefix is empty -- better nothing than a "
        "counter that means nothing");

    /* ================================================================
     * 3. paint -- the window itself
     *
     * The exit status IS the row count, and the next `done` erases exactly
     * that many. Every assertion here is really about that number.
     * ================================================================ */
    osr_sb_env(&sb, "COLUMNS", "80");
    osr_sb_env(&sb, "OSR_TAIL_LINES", "");
    osr_sb_env(&sb, "OSR_DIM", "");
    osr_sb_env(&sb, "OSR_NC", "");

    {
        int painted = ui3("paint", "0", fx("plain.log"), "| building thing");
        osr_assert_true(painted == rows(),
            "paint returns exactly the number of rows it wrote -- the erase "
            "that follows is computed from it, and an overcount eats the "
            "user's scrollback");
        osr_assert_out(&sb, "building thing",
            "the description is on the status line");
        osr_assert_out(&sb, "g",
            "and the TAIL of the log is shown -- the last lines, which are the "
            "ones that say what the command is doing now");
        osr_refute_log(&sb, "a\nb\nc",
            "not the head of it");
    }

    /* An empty log, a missing one, and one that is only blank lines: each
     * still paints the status line and nothing more. Painting zero rows and
     * then erasing one would climb into the scrollback. */
    {
        int p_empty = ui3("paint", "0", fx("empty.log"), "desc");
        osr_assert_true(p_empty == rows(),
            "an empty log still returns an honest row count");
        p_empty = ui3("paint", "0", fx("missing.log"), "desc");
        osr_assert_true(p_empty == rows(),
            "a log that does not exist yet returns an honest row count -- the "
            "first paint happens before the command has written anything");
        p_empty = ui3("paint", "0", fx("blank.log"), "desc");
        osr_assert_true(p_empty == rows(),
            "a log of blank lines returns an honest row count");
    }

    /* A file with no trailing newline: its last line is still a line. */
    ui3("paint", "0", fx("nonl.log"), "desc");
    osr_assert_out(&sb, "no trailing newline",
        "a final line with no newline is still shown");

    /* CRLF and ANSI: a package manager writes both. Carriage returns would
     * make the block redraw over itself; a stray colour sequence would leak
     * into everything painted after it. */
    ui3("paint", "0", fx("crlf.log"), "desc");
    osr_assert_true(strstr(osr_sb_capture(&sb), "one\n") != NULL,
        "a CRLF log line loses its carriage return -- apt writes them, and one "
        "left in the middle of a row redraws that row over itself");
    osr_assert_true(strstr(osr_sb_capture(&sb), "one\r") == NULL,
        "the stripped CR really is gone, not merely followed by a newline "
        "(the leading \\r on each row is the painter's own, not the log's)");

    ui3("paint", "0", fx("ansi.log"), "desc");
    osr_assert_out(&sb, "green",
        "a coloured log line keeps its text");
    osr_assert_true(strstr(osr_sb_capture(&sb), "\033[32m") == NULL,
        "but not its colour: an escape from the log would leak into every row "
        "painted after it");
    osr_assert_true(strstr(osr_sb_capture(&sb), "\033[?25l") == NULL,
        "and a cursor-hide sequence from the log is stripped too -- it would "
        "leave the user with no cursor after the run");

    /* An unterminated escape is what a killed process leaves behind. */
    ui3("paint", "0", fx("badesc.log"), "desc");
    osr_assert_out(&sb, "unterminated",
        "a truncated escape sequence does not swallow the rest of the line");

    /* The width. A line wider than the terminal wraps, which makes the row
     * count wrong, which makes the erase wrong. */
    osr_sb_env(&sb, "COLUMNS", "25");
    {
        int painted = ui3("paint", "0", fx("long.log"), "desc");
        osr_assert_true(longest() <= 25,
            "a 300-character log line is cut to the terminal width -- if it "
            "wrapped, one log line would occupy several rows and the erase "
            "would be short by the difference");
        osr_assert_true(painted == rows(),
            "and the row count still matches what was written");
    }
    osr_sb_env(&sb, "COLUMNS", "80");

    /* How many tail lines. Zero means the block is the status line alone. */
    osr_sb_env(&sb, "OSR_TAIL_LINES", "0");
    {
        int painted = ui3("paint", "0", fx("big.log"), "desc");
        osr_assert_true(painted == rows(),
            "OSR_TAIL_LINES=0 still returns an honest row count");
    }
    osr_sb_env(&sb, "OSR_TAIL_LINES", "2");
    {
        int painted = ui3("paint", "0", fx("big.log"), "desc");
        osr_assert_true(painted == rows(),
            "OSR_TAIL_LINES=2 returns an honest row count");
        osr_assert_out(&sb, "5000",
            "and the tail really is the tail of a 5000-line log");
    }
    osr_sb_env(&sb, "OSR_TAIL_LINES", "");

    /* A description carrying backslashes or printf conversions. The shell
     * original printed the status line with %b, so `\n` in a description
     * expanded; the port keeps that, and a description is never a format. */
    ui3("paint", "0", fx("plain.log"), "percent %s %d");
    osr_assert_out(&sb, "percent %s %d",
        "a description full of printf conversions is not expanded as a format");

    /* ================================================================
     * 4. done -- the erase
     * ================================================================ */
    ui3("done", "0", "[ok] a step", NULL);
    osr_assert_out_is(&sb, "\r\033[2K[ok] a step\n",
        "with nothing painted, done returns to column 0, clears the line it is "
        "about to write on, and writes the result -- and climbs by nothing, "
        "which is the whole point: an unconditional climb eats scrollback");
    osr_assert_true(strstr(osr_sb_capture(&sb), "\033[A") == NULL,
        "no cursor-up at all when no rows were painted");

    ui3("done", "5", "[ok] a step", NULL);
    osr_assert_out(&sb, "[ok] a step",
        "with rows painted, done still ends with the result line");
    osr_assert_out(&sb, "\033[",
        "and it moves the cursor up over what it is replacing");

    /* ================================================================
     * 5. result -- the line a finished step collapses to
     *
     * The row count is read from a state file the spinner wrote, and the file
     * is CONSUMED: leaving it behind would make the next step erase rows this
     * one already replaced.
     * ================================================================ */
    osr_sb_env(&sb, "OSR_GREEN", "");
    osr_sb_env(&sb, "OSR_RED", "");
    osr_sb_write(&sb, "paint.state", "3\n", 0644);
    {
        HStr state;
        hs_init(&state);
        hs_path(&state, hs_text(&sb.root), "paint.state");
        osr_sb_reset(&sb);
        osr_sb_run_core(&sb, "ui", "result", hs_text(&state), "ok",
                        "Installing polybar", (const char *)NULL);
        osr_assert_out(&sb, "[ok]",
            "a successful step collapses to an ASCII [ok] marker (D-2)");
        osr_assert_out(&sb, "Installing polybar",
            "next to the description it was started with");
        hs_free(&state);
    }
    osr_assert_absent(&sb, "paint.state",
        "the row-count state file is consumed, so the next step cannot erase "
        "rows this one already replaced");

    osr_sb_write(&sb, "paint.state", "3\n", 0644);
    {
        HStr state;
        hs_init(&state);
        hs_path(&state, hs_text(&sb.root), "paint.state");
        osr_sb_reset(&sb);
        osr_sb_run_core(&sb, "ui", "result", hs_text(&state), "fail",
                        "Installing polybar", (const char *)NULL);
        osr_assert_out(&sb, "[!!]",
            "a failed step collapses to [!!] -- ASCII, and visibly different "
            "from [ok] without relying on colour");
        hs_free(&state);
    }

    /* ================================================================
     * 6. fail-tail -- what a failed step shows
     *
     * Raw bytes, unfiltered, on stderr. A step that failed has to show
     * exactly what the command printed: this is the one place the window's
     * tidying would hide the answer.
     * ================================================================ */
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "ui", "fail-tail", "20", fx("plain.log"),
                    (const char *)NULL);
    osr_assert_err(&sb, "g", "fail-tail shows the end of the log");
    osr_assert_true(osr_sb_capture(&sb)[0] == '\0',
        "fail-tail writes to stderr, not stdout -- the failure belongs on the "
        "same stream as the error that follows it");

    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "ui", "fail-tail", "20", fx("ansi.log"),
                    (const char *)NULL);
    osr_assert_err(&sb, "\033[32m",
        "and it does NOT strip escapes: a failed step must show exactly what "
        "the command printed, colour and all");

    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "ui", "fail-tail", "20", fx("missing.log"),
                    (const char *)NULL);
    osr_assert_silent(&sb,
        "a step that failed before writing anything dumps nothing rather than "
        "an error about the log");

    /* ================================================================
     * 7. tty-mode -- the auto-degrade decision (SS3)
     * ================================================================ */
    osr_sb_env(&sb, "OSR_VERBOSE", "");
    osr_assert_true(ui1("tty-mode") != 0,
        "piped output is not tty mode: the live window degrades to plain "
        "streamed lines, which is what a log file wants");

    osr_sb_env(&sb, "OSR_VERBOSE", "1");
    osr_assert_true(ui1("tty-mode") != 0,
        "--verbose is not tty mode either, even on a terminal: asking for the "
        "output means asking to see all of it, not a two-line window");
    osr_sb_env(&sb, "OSR_VERBOSE", "");

    /* Off a terminal there is no cursor to hide, and writing the sequence
     * anyway would put escape bytes into a log file. */
    ui1("cursor-hide");
    osr_assert_silent(&sb, "nothing hides the cursor when stdout is not a terminal");
    ui1("cursor-show");
    osr_assert_silent(&sb, "and nothing shows it again");

    /* ================================================================
     * 8. D-2, over everything painted
     * ================================================================ */
    {
        int clean = 1;
        static const char *const logs[] = {
            "plain.log", "ansi.log", "badesc.log", "long.log", "big.log", NULL
        };
        int i;
        for (i = 0; logs[i] != NULL; i++) {
            ui3("paint", "0", fx(logs[i]), "| building thing");
            if (!all_ascii(osr_sb_capture(&sb))) clean = 0;
        }
        ui3("done", "3", "[ok] a step", NULL);
        if (!all_ascii(osr_sb_capture(&sb))) clean = 0;
        osr_assert_true(clean,
            "every byte the window writes is 7-bit ASCII (D-2) -- the spinner "
            "frames are -\\|/ and the markers are [ok]/[!!] for the same "
            "reason: a serial console cannot render anything else");
    }

    osr_sb_free(&sb);
    return osr_finish();
}
