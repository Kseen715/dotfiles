/* modules/sqldeveloper.c -- Oracle SQL Developer from the vendor zip
 * (oracle.com/database/sqldeveloper/technologies/download, the "Other
 * Platforms" download). Oracle ships Linux as one platform-neutral
 * sqldeveloper-<version>-no-jre.zip plus a noarch RPM; the zip needs no
 * package manager and no Oracle account, so every target takes the same route
 * (any.map -> source:provide_sqldeveloper). The tree lands in
 * /opt/sqldeveloper with a /usr/local/bin/sqldeveloper launcher and a menu
 * entry built from the icon.png inside the zip.
 *
 * The JDK is a real dependency, not a nicety: the -no-jre zip carries no Java,
 * and the launcher (ide/bin/launcher.sh) falls back to reading a JDK path off
 * stdin when it cannot find one -- an interactive prompt in the middle of an
 * unattended install. So the JDK 17 package goes in first, and the builder
 * pins it with the SetJavaHome directive in sqldeveloper/bin/jdk.conf, which is
 * the vendor's documented system-wide way to choose one. Pinning matters
 * because on Debian and Ubuntu the `java` on PATH stays whatever
 * update-alternatives already pointed at.
 *
 * Why this calls provide_sqldeveloper directly instead of
 * `pkg_install sqldeveloper`: the source: provider's idempotency probe is
 * `command -v sqldeveloper` (§4), which any installed version satisfies, so a
 * rice listing it would install once and skip forever. The builder compares the
 * installed tree's sqldeveloper/bin/version.properties against the version on
 * the download page, which makes `osr module sqldeveloper` the upgrade path --
 * same shape as modules/datagrip.c.
 *
 * Settings are deliberately not layered here (§5): SQL Developer keeps its
 * config and its connection definitions under ~/.sqldeveloper/, which is user
 * data the IDE rewrites on exit.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/build.h"

#include <stddef.h>

static int build_sqldeveloper(void *ctx) {
    (void)ctx;
    return osr_build_run("provide_sqldeveloper");
}

int osrm_sqldeveloper(void) {
    static const char *const jdk[] = { "jdk17", NULL };
    int ok;

    ok = osr_pkg_install_step("Installing a JDK 17 for SQL Developer", jdk);
    ok = osr_step("Installing Oracle SQL Developer", build_sqldeveloper, NULL) && ok;
    osr_info("SQL Developer installed to /opt/sqldeveloper - run 'sqldeveloper' "
             "or use the menu entry");
    return ok;
}
