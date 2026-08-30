/* test/unit_c/log_test.c -- the five log lines, and which stream each goes to.
 *
 * Every byte a rice prints comes through here, so this file is mostly about
 * two rules that are easy to get wrong and hard to notice:
 *
 *   WHICH STREAM. A warning on stdout is a warning that vanishes when someone
 *   pipes the install into a file and reads the file. Half of what a logger
 *   decides is the stream, so every level below is asserted on the stream it
 *   must have used AND on the one it must have stayed off.
 *
 *   ASCII ONLY (D-2). Every byte written to the terminal is 7-bit. Not a style
 *   rule: busybox on a fresh Alpine, a serial console, LANG=C and a minimal
 *   TERM all turn a box-drawing character or an em dash into mojibake, and the
 *   one machine most likely to be installed over a serial console is the one
 *   least able to render it.
 *
 * The message is passed through verbatim, which matters more than it sounds:
 * a package name can contain a `%`, a Windows path in an error can contain
 * `\n` as two characters, and a logger that hands either to printf as a FORMAT
 * either prints garbage or reads off the end of its arguments.
 *
 * Replaces test/unit/log_c_parity.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

/* say -- one log line, with the palette the scenario has set. */
static int say(const char *level, const char *msg) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "log", level, msg, (const char *)NULL);
}

/* on_stdout / on_stderr -- the line went to that stream and ONLY that one. */
static void on_stdout(const char *expected, const char *label) {
    osr_assert_out_is(&sb, expected, label);
    osr_assert_true(osr_sb_capture_err(&sb)[0] == '\0',
        "  ...and nothing went to stderr");
}
static void on_stderr(const char *expected, const char *label) {
    HStr held;
    hs_init(&held);
    hs_add(&held, osr_sb_scrub(&sb, osr_sb_capture_err(&sb)));
    osr_assert_eq(expected, hs_text(&held), label);
    hs_free(&held);
    osr_assert_true(osr_sb_capture(&sb)[0] == '\0',
        "  ...and nothing went to stdout");
}

/* palette -- the six colour variables, set or blank. */
static void palette(int on) {
    osr_sb_env(&sb, "OSR_RED",    on ? "\033[0;31m" : "");
    osr_sb_env(&sb, "OSR_GREEN",  on ? "\033[0;32m" : "");
    osr_sb_env(&sb, "OSR_YELLOW", on ? "\033[0;33m" : "");
    osr_sb_env(&sb, "OSR_CYAN",   on ? "\033[0;36m" : "");
    osr_sb_env(&sb, "OSR_DIM",    on ? "\033[2m"    : "");
    osr_sb_env(&sb, "OSR_NC",     on ? "\033[0m"    : "");
}

/* all_ascii -- is every byte of `text` 7-bit and printable-or-whitespace?
 * ANSI SGR colour is ASCII bytes (ESC is 0x1b), so a coloured line passes. */
static int all_ascii(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    for (; *p != '\0'; p++) {
        if (*p > 0x7f) return 0;
    }
    return 1;
}

int main(void) {
    osr_sb_init(&sb);
    palette(0);
    osr_sb_env(&sb, "OSR_DEBUG", "");

    /* ================================================================
     * 1. The five levels, plain
     *
     * The tags are fixed-width so the messages line up in a log file read
     * later, and they are the ASCII forms D-2 requires -- [DONE], not a check
     * mark.
     * ================================================================ */
    say("info", "Installing polybar");
    on_stdout("[INFO]  Installing polybar\n",
        "info goes to stdout: it is the normal narration of a run");

    say("success", "Installing polybar");
    on_stdout("[DONE]  Installing polybar\n",
        "success goes to stdout, tagged [DONE] in ASCII rather than a glyph (D-2)");

    say("warn", "polybar is held - skipping");
    on_stderr("[WARN]  polybar is held - skipping\n",
        "warn goes to STDERR, so it survives a run being piped to a file");

    osr_assert_rc(say("error", "module not found: zsh"), 1,
        "error exits non-zero: it is the fatal one");
    on_stderr("[ERROR] module not found: zsh\n",
        "error goes to stderr and prints nothing on stdout");

    /* ================================================================
     * 2. debug is gated
     *
     * A rice install is long, and debug output is what makes a real failure
     * scroll past. It is off unless asked for.
     * ================================================================ */
    say("debug", "resolved zsh -> zsh");
    osr_assert_silent(&sb,
        "debug prints nothing at all without $OSR_DEBUG");

    osr_sb_env(&sb, "OSR_DEBUG", "1");
    say("debug", "resolved zsh -> zsh");
    on_stderr("[DEBUG] resolved zsh -> zsh\n",
        "debug prints to STDERR when $OSR_DEBUG is set -- it is diagnostic "
        "chatter, not part of the narration a user piped to a file");
    osr_sb_env(&sb, "OSR_DEBUG", "");

    /* ================================================================
     * 3. The message is data, never a format
     * ================================================================ */
    say("info", "progress %s %d %%");
    on_stdout("[INFO]  progress %s %d %%\n",
        "a message full of printf conversions is printed verbatim -- passing "
        "it as a FORMAT would read off the end of the argument list");

    say("info", "path C:\\new\\temp");
    on_stdout("[INFO]  path C:\\new\\temp\n",
        "a Windows path keeps its backslashes: \\n here is two characters, not "
        "a newline");

    say("info", "");
    on_stdout("[INFO]  \n",
        "an empty message still prints its tag rather than nothing at all");

    /* ================================================================
     * 4. The palette
     *
     * The colours are INHERITED, not decided here: the decision is made once
     * against the real terminal in osr.c's startup_env, because a module runs
     * with its stdout on a log file and would otherwise come out colourless
     * inside a coloured run.
     * ================================================================ */
    palette(1);
    say("info", "Installing polybar");
    osr_assert_out_is(&sb, "\033[0;36m[INFO]  \033[0mInstalling polybar\n",
        "info wears the cyan from the environment -- the tag AND the padding "
        "after it, so the message itself is never coloured");

    say("warn", "held");
    {
        HStr held;
        hs_init(&held);
        hs_add(&held, osr_sb_scrub(&sb, osr_sb_capture_err(&sb)));
        osr_assert_eq("\033[0;33m[WARN]  \033[0mheld\n", hs_text(&held),
            "warn wears yellow");
        hs_free(&held);
    }

    say("success", "done");
    osr_assert_out_is(&sb, "\033[0;32m[DONE]  \033[0mdone\n",
        "success wears green");

    say("error", "boom");
    {
        HStr held;
        hs_init(&held);
        hs_add(&held, osr_sb_scrub(&sb, osr_sb_capture_err(&sb)));
        osr_assert_eq("\033[0;31m[ERROR] \033[0mboom\n", hs_text(&held),
            "error wears red -- and its tag is one character wider, so it "
            "carries one space of padding rather than two and the messages "
            "still line up");
        hs_free(&held);
    }

    /* A colour that is set must always be closed. An unterminated SGR sequence
     * leaves the user's terminal tinted for every command they run afterwards. */
    say("info", "tinted");
    osr_assert_out(&sb, "\033[0m",
        "every coloured line ends its sequence -- an unclosed one tints the "
        "user's shell until they reset it");

    /* ================================================================
     * 5. Everything is ASCII (D-2)
     * ================================================================ */
    {
        static const char *const levels[] = {
            "info", "warn", "success", "debug", "error", NULL
        };
        int i;
        int clean = 1;
        osr_sb_env(&sb, "OSR_DEBUG", "1");
        for (i = 0; levels[i] != NULL; i++) {
            say(levels[i], "a message");
            if (!all_ascii(osr_sb_capture(&sb))) clean = 0;
            if (!all_ascii(osr_sb_capture_err(&sb))) clean = 0;
        }
        osr_assert_true(clean,
            "every level writes 7-bit ASCII only (D-2) -- a serial console, "
            "busybox or LANG=C would turn anything else into mojibake");
        osr_sb_env(&sb, "OSR_DEBUG", "");
    }
    palette(0);

    /* ================================================================
     * 6. The step counter
     *
     * The runner prefixes each module with its position, and the width is
     * fixed so the module names line up in a log nobody is watching live.
     * ================================================================ */
    osr_sb_env(&sb, "OSR_STEP_N", "3");
    osr_sb_env(&sb, "OSR_STEP_TOTAL", "12");
    say("step", "module: zsh");
    on_stdout("[INFO]  [03/12] module: zsh\n",
        "the step line is zero-padded to a fixed width, so module names align");

    osr_sb_env(&sb, "OSR_STEP_N", "7");
    osr_sb_env(&sb, "OSR_STEP_TOTAL", "7");
    say("step", "module: zsh");
    on_stdout("[INFO]  [07/07] module: zsh\n",
        "the last step reads N of N rather than being special-cased");

    /* A total of zero is "I do not know how many" -- a manifest that has not
     * been counted yet. Printing [00/00] would be a lie about progress. */
    osr_sb_env(&sb, "OSR_STEP_N", "0");
    osr_sb_env(&sb, "OSR_STEP_TOTAL", "0");
    say("step", "module: zsh");
    on_stdout("[INFO]  module: zsh\n",
        "with no total known, the counter is omitted rather than shown as 00/00");

    osr_sb_free(&sb);
    return osr_finish();
}
