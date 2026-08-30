/* lib/preflight.h -- rice preconditions, checked before any mutation
 * (§10 Tier 1). The C port of lib/preflight.sh.
 *
 * A rice declares `require: <predicate>` lines in its manifest; the runner
 * collects them and calls osr_preflight BEFORE step 1, exiting non-zero with
 * nothing written when a predicate is unmet. Predicates are cheap and
 * data-only (no installs) -- a functional capability probe (Vulkan init) is an
 * early module instead (§10 Tier 2), not a require: predicate.
 *
 * Predicates (DESIGN §10 table):
 *   arch:<m>       uname -m / OSR_ARCH matches
 *   init:<i>       OSR_INIT matches (systemd/openrc/runit/sysvinit)
 *   distro:<d>     OSR_DISTRO matches
 *   release:<c>    OSR_CODENAME or OSR_VERSION_ID matches
 *   cmd:<bin>      command -v <bin> succeeds
 *   gpu:present    a GPU exists (OSR_DRI:-/dev/dri/renderD* or OSR_GPU_COUNT)
 *
 * The value half may list alternatives with `|`, and the predicate holds when
 * ANY of them does: `require: distro:void|debian|ubuntu`. That is the shape a
 * rice actually needs -- "this manifest resolves on these package managers" is
 * a set, not a single value, and one require: line per distro would read as a
 * conjunction and never be satisfiable. `|` is the only combinator: AND is
 * what writing two require: lines already means.
 *
 * C89 + POSIX.
 */
#ifndef OSR_PREFLIGHT_H
#define OSR_PREFLIGHT_H

/* osr_preflight_check -- 1 when the host satisfies the predicate. The
 * detection variables (OSR_*) come from osr_detect, so run this after it. An
 * unknown predicate warns and counts as satisfied: a manifest from a newer
 * rice must not fail closed on a tag this build has never heard of. */
int osr_preflight_check(const char *predicate);

/* osr_preflight -- check each of preds (NULL-terminated); the first unmet one
 * is fatal, before any module has run. */
void osr_preflight(const char *const preds[]);

#endif /* OSR_PREFLIGHT_H */
