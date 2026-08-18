/* test/unit_c/wintweak_test.c -- lib/wintweak.c's parsers and the two
 * policy tables in modules/win-tweaks.c.
 *
 * Deliberately touches nothing: not one assertion here calls a verb that
 * writes a registry value, stops a service or deletes a path. Those cannot
 * be unit-tested at all -- their whole effect is on the machine running
 * them, and the machine running them is a developer's. What CAN be tested,
 * and is the part a port gets wrong, is the data: whether every setting the
 * retired windows-11-x86_64/setup.ps1 applied is still here, under the same
 * key, with the same value, and whether the rows it deliberately did not
 * apply are still deliberately not applied.
 *
 * So this file is the regression test for the ingest itself. If a row is
 * dropped, renamed or flipped, it fails here rather than on somebody's
 * desktop.
 */
#include "../c_test.h"
#include "../../lib/wintweak.h"
#include "../../modules/src/common.h"

#include <string.h>

/* --- the key parser ------------------------------------------------------- */

static void test_split_key(void) {
    osr_wintweak_hive hive;
    char subkey[256];

    osr_t_true("split: the ps1 spelling HKCU:Software\\X parses",
        osr_wintweak_split_key("HKCU:Software\\Microsoft", &hive, subkey, sizeof(subkey)));
    osr_t_eq_int("split: ...into HKCU", hive, OSR_WINTWEAK_HIVE_HKCU);
    osr_t_eq_str("split: ...with the colon and separators stripped",
        subkey, "Software\\Microsoft");

    osr_t_true("split: PowerShell's own HKLM:\\ form parses",
        osr_wintweak_split_key("HKLM:\\Software\\Sudo", &hive, subkey, sizeof(subkey)));
    osr_t_eq_int("split: ...into HKLM", hive, OSR_WINTWEAK_HIVE_HKLM);
    osr_t_eq_str("split: ...with both separators stripped", subkey, "Software\\Sudo");

    osr_t_true("split: the long form parses too",
        osr_wintweak_split_key("HKEY_CURRENT_USER\\Console", &hive, subkey, sizeof(subkey)));
    osr_t_eq_int("split: ...into HKCU", hive, OSR_WINTWEAK_HIVE_HKCU);
    osr_t_eq_str("split: ...leaving the subkey", subkey, "Console");

    osr_t_true("split: case does not matter",
        osr_wintweak_split_key("hkcu:software", &hive, subkey, sizeof(subkey)));

    osr_t_true("split: an unsupported hive is rejected, not guessed",
        !osr_wintweak_split_key("HKCR:.txt", &hive, subkey, sizeof(subkey)));
    osr_t_eq_int("split: ...and reports no hive", hive, OSR_WINTWEAK_HIVE_NONE);

    osr_t_true("split: a hive with no subkey is rejected",
        !osr_wintweak_split_key("HKCU:", &hive, subkey, sizeof(subkey)));

    /* A prefix match alone is not enough: this must not resolve to
     * HKCU\STOM\Foo, which is a real key that would take the write. */
    osr_t_true("split: a longer name that merely starts with a hive is rejected",
        !osr_wintweak_split_key("HKCUSTOM\\Foo", &hive, subkey, sizeof(subkey)));

    /* A truncated registry path is the one failure mode that would write to
     * the wrong key rather than fail, so it must be refused outright. */
    osr_t_true("split: a subkey too long for the buffer is refused, not truncated",
        !osr_wintweak_split_key("HKCU:Software\\Microsoft\\Windows", &hive, subkey, 8));
    osr_t_eq_str("split: ...leaving the output empty", subkey, "");
}

static void test_names(void) {
    osr_t_eq_str("hive_name: HKCU", osr_wintweak_hive_name(OSR_WINTWEAK_HIVE_HKCU), "HKCU");
    osr_t_eq_str("hive_name: HKLM", osr_wintweak_hive_name(OSR_WINTWEAK_HIVE_HKLM), "HKLM");
    osr_t_eq_str("start_name: Manual",
        osr_wintweak_start_name(OSR_WINTWEAK_START_MANUAL), "Manual");
    osr_t_eq_str("start_name: Disabled",
        osr_wintweak_start_name(OSR_WINTWEAK_START_DISABLED), "Disabled");
    osr_t_eq_str("start_name: KEEP reads as unchanged",
        osr_wintweak_start_name(OSR_WINTWEAK_START_KEEP), "unchanged");
}

/* --- the registry table --------------------------------------------------- */

/* find_reg -- the row for `name`, or NULL. Rows are looked up by value name
 * rather than by index so that reordering the table (a cosmetic change)
 * never fails a test, while losing a row (a real one) always does. */
static const osr_wintweak_reg *find_reg(const char *name) {
    unsigned long count = 0;
    const osr_wintweak_reg *rows = osrm_win_reg_tweaks(&count);
    unsigned long i;
    for (i = 0; i < count; i++) {
        if (strcmp(rows[i].name, name) == 0) return &rows[i];
    }
    return NULL;
}

/* check_reg -- one row: present, applied-or-not, and carrying the value
 * setup.ps1 passed its microscript. */
static void check_reg(const char *name, unsigned long value, int enabled) {
    const osr_wintweak_reg *row = find_reg(name);
    char label[160];

    sprintf(label, "reg: %s is still in the table", name);
    osr_t_true(label, row != NULL);
    if (row == NULL) return;

    sprintf(label, "reg: %s value", name);
    osr_t_eq_int(label, row->value, value);

    sprintf(label, "reg: %s enabled flag", name);
    osr_t_eq_int(label, row->enabled, enabled);
}

static void test_reg_table(void) {
    unsigned long count = 0;
    const osr_wintweak_reg *rows = osrm_win_reg_tweaks(&count);
    const osr_wintweak_reg *sudo_row;
    osr_wintweak_hive hive;
    char subkey[512];
    unsigned long i;

    osr_t_true("reg: the table is not empty", count > 0 && rows != NULL);

    /* Explorer + taskbar, with the values setup.ps1 passed. */
    check_reg("HideFileExt", 0, 1);
    check_reg("Hidden", 1, 1);
    check_reg("ShowCortanaButton", 0, 1);
    check_reg("TaskbarEndTask", 1, 1);
    check_reg("DisallowShaking", 1, 1);

    /* The whole snap group is off -- that was the point of it. */
    check_reg("EnableTaskGroups", 0, 1);
    check_reg("SnapAssist", 0, 1);
    check_reg("EnableSnapBar", 0, 1);
    check_reg("EnableSnapAssistFlyout", 0, 1);
    check_reg("DITest", 0, 1);

    /* sudo: inline mode, and the one machine-wide row. */
    check_reg("Enabled", 3, 1);
    sudo_row = find_reg("Enabled");
    if (sudo_row != NULL) {
        osr_t_true("reg: the sudo row lives in HKLM",
            osr_wintweak_split_key(sudo_row->key, &hive, subkey, sizeof(subkey))
            && hive == OSR_WINTWEAK_HIVE_HKLM);
    }

    /* Carried but not applied: reg-dont-pretty-path.ps1 existed, setup.ps1
     * never called it. Keeping the row off is the ported behavior; turning
     * it on would be a new decision. */
    check_reg("DontPrettyPath", 1, 0);

    /* Every row must name a hive this code can actually write. */
    for (i = 0; i < count; i++) {
        if (!osr_wintweak_split_key(rows[i].key, &hive, subkey, sizeof(subkey))) {
            char label[200];
            sprintf(label, "reg: row '%s' has an unusable key path", rows[i].name);
            osr_t_fail_msg(label, rows[i].key);
            return;
        }
    }
    osr_t_ok("reg: every row's key path parses");

    osr_t_true("reg: the table needs admin (it has an enabled HKLM row)",
        osr_wintweak_needs_admin_reg(rows, count));
}

/* --- the service table ---------------------------------------------------- */

static const osr_wintweak_service *find_service(const char *service) {
    unsigned long count = 0;
    const osr_wintweak_service *rows = osrm_win_service_tweaks(&count);
    unsigned long i;
    for (i = 0; i < count; i++) {
        if (strcmp(rows[i].service, service) == 0) return &rows[i];
    }
    return NULL;
}

static void check_service(const char *service, int stop, osr_wintweak_start start, int enabled) {
    const osr_wintweak_service *row = find_service(service);
    char label[160];

    sprintf(label, "svc: %s is still in the table", service);
    osr_t_true(label, row != NULL);
    if (row == NULL) return;

    sprintf(label, "svc: %s stop flag", service);
    osr_t_eq_int(label, row->stop, stop);

    sprintf(label, "svc: %s start type", service);
    osr_t_eq_int(label, row->start, start);

    sprintf(label, "svc: %s enabled flag", service);
    osr_t_eq_int(label, row->enabled, enabled);
}

static void test_service_table(void) {
    unsigned long count = 0;
    const osr_wintweak_service *rows = osrm_win_service_tweaks(&count);
    const osr_wintweak_service *row;

    osr_t_true("svc: the table is not empty", count > 0 && rows != NULL);

    check_service("DiagTrack", 1, OSR_WINTWEAK_START_DISABLED, 1);
    check_service("DPS", 1, OSR_WINTWEAK_START_DISABLED, 1);
    check_service("dmwappushservice", 1, OSR_WINTWEAK_START_DISABLED, 1);
    check_service("WSearch", 1, OSR_WINTWEAK_START_DISABLED, 1);
    check_service("SysMain", 1, OSR_WINTWEAK_START_DISABLED, 1);

    /* setup.ps1 had this line commented out. It stays commented out. */
    check_service("Fax", 1, OSR_WINTWEAK_START_DISABLED, 0);

    /* The one row that is emphatically NOT a disable: Windows Update goes
     * to Manual and is never stopped. A future edit that disables it would
     * leave the machine unpatched, so it is asserted, not assumed. */
    check_service("wuauserv", 0, OSR_WINTWEAK_START_MANUAL, 1);

    /* The two cache purges the ps1 files did, still attached to the service
     * that owns each cache. */
    row = find_service("DiagTrack");
    if (row != NULL) {
        osr_t_true("svc: DiagTrack still purges its ETL logs",
            row->purge != NULL && strstr(row->purge, "Diagnosis") != NULL);
    }
    row = find_service("WSearch");
    if (row != NULL) {
        osr_t_true("svc: WSearch still purges its index",
            row->purge != NULL && strstr(row->purge, "Search") != NULL);
    }

    osr_t_true("svc: the table needs admin", osr_wintweak_needs_admin_services(rows, count));
}

int main(void) {
    OSR_T_INIT();
    test_split_key();
    test_names();
    test_reg_table();
    test_service_table();
    return osr_t_finish();
}
