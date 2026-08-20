/* lib/uv/backend.h -- the interface every undervolting backend implements.
 *
 * `osr undervolt` is one vendor-neutral engine (search, stress ladder, crash
 * journal) sitting on top of a very small per-vendor shim. This header is that
 * shim, and it is deliberately the narrowest thing that still lets the engine
 * be written once: four verbs and a capability report.
 *
 * The backends:
 *
 *   amd_smu      Curve Optimizer through the out-of-tree ryzen_smu driver.
 *                Per-core only -- AMD exposes no separate cache/iGPU plane to
 *                userspace, and on Granite Ridge (Ryzen 9000) not even a
 *                readback. x86_64 only.
 *   intel_msr    the OC mailbox at MSR 0x150. The one backend that fills in
 *                all four domains, and the one most likely to be locked shut
 *                by firmware (CVE-2019-11157). x86_64 only.
 *   arm_dt       device-tree opp-microvolt rewriting. aarch64 only, and the
 *                odd one out -- see `settings_volatile` below.
 *   generic_opp  the last resort: report what the hardware exposes, write
 *                nothing. Always compiled, always claims the machine if no
 *                other backend did.
 *
 * THE ONE INVARIANT WORTH READING TWICE. On the x86 backends the offset lives
 * in a mailbox that a power cycle clears, so an undervolt that hard-locks the
 * box costs a reboot and nothing else. That single property is what makes an
 * automatic search safe enough to attempt at all, and the engine leans on it
 * everywhere. `arm_dt` does not have it -- its setting is consumed at boot and
 * survives the crash -- which is why `settings_volatile` exists rather than
 * being assumed, and why that backend carries a two-phase commit of its own.
 *
 * A probe never mutates anything. It is safe to run on any machine, including
 * a VM with no voltage control whatsoever, and that is the contract that lets
 * `osr undervolt cpu probe` be the first thing anyone runs.
 *
 * C89 + POSIX.
 */
#ifndef OSR_UV_BACKEND_H
#define OSR_UV_BACKEND_H

#include "../common.h"

/* --- domains -------------------------------------------------------------
 *
 * The separately-adjustable voltage planes. Which ones exist is hardware, not
 * policy: Intel has all four, AMD has UV_CORE alone, an ARM OPP table usually
 * has one shared rail that lands in UV_CORE too. A backend reports what is
 * really there and the engine skips the rest -- nothing here is faked so that
 * the table looks complete.
 */
typedef enum {
    UV_CORE = 0,   /* the cores themselves; per-core where the hardware allows */
    UV_CACHE,      /* L3 / ring. Intel ties this to the core rail on most parts */
    UV_GPU,        /* integrated graphics */
    UV_UNCORE,     /* system agent / uncore */
    UV_DOMAIN_MAX
} UvDomain;

/* uv_domain_name -- "core" / "cache" / "gpu" / "uncore", for reports and for
 * the --core/--cache/--gpu/--uncore option names. */
const char *uv_domain_name(UvDomain d);
/* uv_domain_parse -- the inverse; UV_DOMAIN_MAX when the word is not one. */
UvDomain uv_domain_parse(const char *name);

/* --- return codes --------------------------------------------------------
 *
 * 1 for success and 0 for failure, the same contract lib/module.h states for
 * a module, with one addition: a read can legitimately be impossible on
 * hardware that only accepts writes (Granite Ridge), and the engine must tell
 * that apart from a read that failed.
 */
#define UV_OK       1
#define UV_ERR      0
#define UV_ENOREAD (-1)

/* --- capabilities --------------------------------------------------------- */

#define UV_DETAIL_MAX 256

typedef struct {
    const char *backend;            /* "amd-smu" | "intel-msr" | ... */
    char detail[UV_DETAIL_MAX];     /* one line: codename, SMU version, or why not */

    int present[UV_DOMAIN_MAX];     /* the hardware has this plane */
    int readable[UV_DOMAIN_MAX];    /* ...and we can read its current offset */
    int writable[UV_DOMAIN_MAX];    /* ...and we can actually change it */
    int count[UV_DOMAIN_MAX];       /* addressable units (cores for UV_CORE, else 1) */

    int min_mv;                     /* most negative offset the backend accepts */
    int max_mv;                     /* most positive (overvolting is refused above 0) */
    int step_mv;                    /* the search's coarse step for this hardware */

    /* 1 when a power cycle restores stock by itself -- true for every mailbox
     * backend, false for arm_dt, and the difference decides how much the
     * engine is willing to risk per step. */
    int settings_volatile;
    /* 1 when a change only takes effect after a reboot (arm_dt), which turns
     * each search step from seconds into a full boot cycle. */
    int needs_reboot;
} UvCaps;

/* uv_caps_init -- zero the struct and set the "nothing is possible" defaults,
 * so a probe only has to fill in what it found. */
void uv_caps_init(UvCaps *caps);
/* uv_caps_any_writable -- is there any plane at all we could tune? `auto`
 * refuses to start when this is 0. */
int uv_caps_any_writable(const UvCaps *caps);

/* --- the backend ---------------------------------------------------------- */

typedef struct UvBackend {
    const char *name;

    /* probe -- does this backend drive this machine, and to what extent?
     *
     * Returns 1 when it claims the machine (even if the answer is "claimed,
     * but firmware has it locked" -- that is still this backend's verdict to
     * report, not the next one's), 0 to pass. Fills caps, and appends its
     * human-readable findings to report, which is what `probe` prints.
     *
     * MUST NOT mutate anything. The one exception a backend may make is a
     * write-then-restore round-trip used to detect a firmware lock, which
     * intel_msr needs; it restores the original value unconditionally.
     */
    int (*probe)(UvCaps *caps, Str *report);

    /* read -- current offset in mV for one addressable unit. UV_ENOREAD on
     * hardware with no readback, which is not an error. */
    int (*read)(UvDomain d, int idx, int *mv);

    /* write -- set one unit's offset. The engine journals its intent and
     * fsyncs BEFORE calling this, because this is the call that can take the
     * machine down between one instruction and the next. */
    int (*write)(UvDomain d, int idx, int mv);

    /* reset -- every plane back to stock. Must be safe to call when the
     * current state is unknown, because after a crash it always is. */
    int (*reset)(void);
} UvBackend;

/* uv_detect -- the first backend that claims this machine, with caps and the
 * report filled in. Never NULL: generic_opp claims whatever is left, so the
 * worst case is a backend that can only describe. */
const UvBackend *uv_detect(UvCaps *caps, Str *report);

/* The backends themselves, for uv_detect's table and for the tests. Which of
 * these exist is decided by nob.c and the arch guards in their own files. */
extern const UvBackend uv_backend_generic_opp;

#endif /* OSR_UV_BACKEND_H */
