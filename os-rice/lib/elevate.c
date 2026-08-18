/* lib/elevate.c -- see lib/elevate.h. C89. */
#include "elevate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "winui.h"

static char g_args[2048];
static int g_have_args = 0;
static int g_attempted = 0;

/* append_arg -- add one argument to the saved command line, quoting it when
 * it contains spaces. Bounded: an argument that would overflow is dropped
 * rather than truncated, since a truncated path is worse than a missing
 * one (the child would act on the wrong file instead of defaulting).
 */
static void append_arg(char *dst, unsigned long dst_sz, const char *arg) {
    unsigned long len = (unsigned long)strlen(dst);
    unsigned long need = (unsigned long)strlen(arg) + 4;

    if (len + need >= dst_sz) return;
    if (len > 0) { dst[len] = ' '; len++; dst[len] = '\0'; }

    if (strchr(arg, ' ') != NULL) sprintf(dst + len, "\"%s\"", arg);
    else strcpy(dst + len, arg);
}

void osr_elevate_init(int argc, char **argv) {
    int i;
    char home[MAX_PATH];

    g_args[0] = '\0';
    for (i = 1; i < argc; i++) append_arg(g_args, sizeof(g_args), argv[i]);

    /* This port's $SUDO_USER -- see elevate.h. Only added when the caller
     * did not already pass one, so an elevated child re-invoking itself
     * cannot stack duplicates. */
    if (strstr(g_args, "--user-home") == NULL &&
        GetEnvironmentVariableA("USERPROFILE", home, (DWORD)sizeof(home)) > 0) {
        append_arg(g_args, sizeof(g_args), "--user-home");
        append_arg(g_args, sizeof(g_args), home);
    }

    g_have_args = 1;
}

/* osr_is_admin -- CheckTokenMembership against BUILTIN\Administrators
 * rather than GetTokenInformation(TokenElevation): the latter needs
 * _WIN32_WINNT >= 0x0600 to even declare, and nob.c builds this tree at
 * 0x0501. The answer is the same either way -- under UAC a non-elevated
 * admin carries that SID deny-only, and CheckTokenMembership reports a
 * deny-only SID as not a member.
 */
int osr_is_admin(void) {
    SID_IDENTIFIER_AUTHORITY nt_authority = { SECURITY_NT_AUTHORITY };
    PSID admins;
    BOOL member;

    admins = NULL;
    member = FALSE;

    if (!AllocateAndInitializeSid(&nt_authority, 2,
            SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &admins)) {
        return 0;
    }

    if (!CheckTokenMembership(NULL, admins, &member)) member = FALSE;
    FreeSid(admins);

    return member != FALSE;
}

void osr_set_user_home(const char *home) {
    if (home == NULL || home[0] == '\0') return;
    /* Both spellings: lib/config_copy.c's osr_expand_home reads USERPROFILE
     * on Windows, and HOME is what anything ported from the sh side looks
     * at first. */
    SetEnvironmentVariableA("USERPROFILE", home);
    SetEnvironmentVariableA("HOME", home);
}

int osr_elevate_now(const char *reason) {
    SHELLEXECUTEINFOA sei;
    char exe[MAX_PATH];
    DWORD code;
    DWORD err;

    if (osr_is_admin()) return 1;

    /* One prompt per process, declined or not: a user who says no should
     * not be asked again by the next package in the same run. */
    if (g_attempted) return 0;
    g_attempted = 1;

    if (!g_have_args) {
        osr_warn("cannot elevate: osr_elevate_init was never called");
        return 0;
    }
    if (GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe)) == 0) {
        osr_warn("cannot elevate: this executable's own path is unknown");
        return 0;
    }

    osr_info("%s", reason);
    osr_info("requesting Administrator rights -- one prompt covers this whole run,");
    osr_info("which continues in the elevated window UAC opens.");

    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = "runas";
    sei.lpFile = exe;
    sei.lpParameters = g_args;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExA(&sei)) {
        err = GetLastError();
        if (err == ERROR_CANCELLED) {
            osr_warn("elevation declined -- continuing without Administrator rights");
        } else {
            osr_warn("elevation failed (error %lu) -- continuing without Administrator rights",
                     (unsigned long)err);
        }
        return 0;
    }

    WaitForSingleObject(sei.hProcess, INFINITE);
    code = 0;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);

    /* The elevated child did the run. Nothing is left for this process to
     * do but carry its result back to the shell that started us. */
    exit((int)code);
    return 1; /* not reached */
}

#else /* !_WIN32 */

/* Elevation on Linux is sudo, and that lives in install.sh / lib/user.sh
 * where the rest of the POSIX sh side already handles it -- nothing to
 * port back into the C core.
 */

void osr_elevate_init(int argc, char **argv) {
    (void)argc;
    (void)argv;
}

int osr_is_admin(void) {
    return 0;
}

void osr_set_user_home(const char *home) {
    (void)home;
}

int osr_elevate_now(const char *reason) {
    (void)reason;
    return 0;
}

#endif /* _WIN32 */
