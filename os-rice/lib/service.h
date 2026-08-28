/* lib/service.h -- universal, idempotent service control, the C port of
 * lib/service.sh (§8 G3).
 *
 * Two verbs work on any init, so no module ever calls systemctl directly
 * again. The init is OSR_INIT, detected once; the current state is checked
 * before acting, which is what makes a rerun a no-op.
 *
 * C89 + POSIX.
 */
#ifndef OSR_SERVICE_H
#define OSR_SERVICE_H

#include "common.h"

/* osr_service_resolve -- the logical name mapped to this init's real unit
 * name through lib/servicemap. A row key may carry an optional @<init>
 * qualifier and the most specific match wins -- `<name>@<init>` before the
 * bare `<name>` -- mirroring pkgmap's facets (§1a). That is what lets one
 * logical name cover units whose NAME differs per init (bluetooth.service on
 * systemd, /etc/sv/bluetoothd on runit) without any module growing an init
 * `case`. An unlisted name is its own unit.
 */
void osr_service_resolve(Str *out, const char *name);

/* osr_service_enable -- enable + start now. Declared in module.h too: it is
 * part of what a C module may assume. */
int osr_service_enable(const char *name);

/* osr_service_disable -- stop + disable. */
int osr_service_disable(const char *name);

#endif /* OSR_SERVICE_H */
