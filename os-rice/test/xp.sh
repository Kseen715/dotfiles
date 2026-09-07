#!/bin/sh
# test/xp.sh — the Windows XP tier's acceptance check (PLAN_UNIVERSAL.md,
# phase 0/2). Cross-builds the tree for the XP target and then asks the one
# question the compiler cannot: does the produced PE actually load on XP?
#
# Compiling clean at -D_WIN32_WINNT=0x0501 is necessary but not sufficient.
# A binary passes that and still dies at load time on XP if it imports a
# DLL XP does not ship, a symbol added after XP (mingw declares a few
# without a version guard, and GetProcAddress use is invisible to the
# compiler either way), or carries a subsystem version above 4.0. So the
# check is on the linked image, not on the sources.
#
#   sh test/xp.sh              cross-build, then check build/osr.exe
#   sh test/xp.sh some.exe     check an already-built image only
#
# Skips (exit 0) when no i686 mingw-w64 cross toolchain is installed: a
# POSIX-only checkout must not fail its suite over a tier it cannot build.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/.." && pwd)

CROSS_CC=i686-w64-mingw32-gcc
OBJDUMP=i686-w64-mingw32-objdump

# DLLs Windows XP ships. An import from anything else is a load failure on
# XP even when every symbol in it exists.
XP_DLLS="ADVAPI32 COMCTL32 COMDLG32 GDI32 IPHLPAPI KERNEL32 MSVCRT MPR NETAPI32
OLE32 OLEAUT32 PSAPI SHELL32 SHLWAPI USER32 USERENV VERSION WININET WINMM
WINSPOOL WS2_32 WSOCK32"

# Symbols that exist in mingw's import libraries but not in XP's DLLs. Not
# exhaustive -- it is the set this tree could plausibly reach for, kept as a
# tripwire for the ones a future edit is most likely to introduce.
POST_XP_SYMS="GetTickCount64 InitializeCriticalSectionEx CreateSymbolicLinkA
CreateSymbolicLinkW GetFinalPathNameByHandleA GetFinalPathNameByHandleW
InitOnceExecuteOnce CreateThreadpoolWork SetThreadpoolWait InetNtopA InetPtonA
RegDeleteKeyExA RegDeleteKeyExW RegSetKeyValueA RegGetValueA RegGetValueW
GetUserDefaultLocaleName GetLocaleInfoEx CompareStringEx GetSystemTimePreciseAsFileTime
GetThreadId GetProcessIdOfThread QueryFullProcessImageNameA WSAPoll
GetDpiForSystem SetProcessDPIAware CancelIoEx GetQueuedCompletionStatusEx
FlsAlloc FlsGetValue SetDefaultDllDirectories AddDllDirectory PathCchCombine
K32GetProcessMemoryInfo IsWow64Process2 GetLogicalProcessorInformationEx"

OSR_RED='' OSR_GREEN='' OSR_YELLOW='' OSR_CYAN='' OSR_NC=''
if [ -x "$OSR_ROOT/build/osr" ]; then
    eval "$("$OSR_ROOT/build/osr" ui vars 2>/dev/null || :)"
fi
sec()    { printf '%b%s%b\n' "$OSR_CYAN" "$*" "$OSR_NC"; }
p_ok()   { printf '  %bok%b   %s\n' "$OSR_GREEN" "$OSR_NC" "$*"; }
p_skip() { printf '  %bskip%b %s\n' "$OSR_YELLOW" "$OSR_NC" "$*"; }
p_fail() { printf '  %bFAIL%b %s\n' "$OSR_RED" "$OSR_NC" "$*" >&2; }

FAILED=0

if ! command -v "$OBJDUMP" >/dev/null 2>&1; then
    sec "Windows XP tier:"
    p_skip "no $OBJDUMP -- install a mingw-w64 i686 cross toolchain to check this tier"
    exit 0
fi

BIN=${1:-}
if [ -z "$BIN" ]; then
    command -v "$CROSS_CC" >/dev/null 2>&1 || {
        sec "Windows XP tier:"
        p_skip "no $CROSS_CC -- cannot cross-build the tier"
        exit 0
    }
    sec "Windows XP tier: cross-build ($CROSS_CC)"
    # A target switch invalidates every object (nob notices via build/cc.stamp
    # and rebuilds), so this leaves build/ holding XP objects, not host ones.
    ( cd "$OSR_ROOT" && CC="$CROSS_CC" ./nob >/dev/null ) || {
        p_fail "cross-build failed"
        exit 1
    }
    BIN="$OSR_ROOT/build/osr.exe"
    p_ok "built $BIN"
else
    sec "Windows XP tier: $BIN"
fi

[ -f "$BIN" ] || { p_fail "no such image: $BIN"; exit 1; }

# 1. 32-bit i386 PE. XP x64 exists but is not the tier; a 64-bit image on
#    32-bit XP does not load at all.
if $OBJDUMP -f "$BIN" | grep -q 'pei-i386'; then
    p_ok "PE32, i386"
else
    p_fail "not a 32-bit i386 PE ($($OBJDUMP -f "$BIN" | grep 'file format'))"
    FAILED=1
fi

# 2. Subsystem version. XP refuses to load an image asking for 6.0 (Vista);
#    4.0 is what mingw's i686 target emits and what XP wants.
MAJOR=$($OBJDUMP -p "$BIN" | awk '/MajorSubsystemVersion/ {print $2}')
MINOR=$($OBJDUMP -p "$BIN" | awk '/MinorSubsystemVersion/ {print $2}')
case "$MAJOR" in
    4|5) p_ok "subsystem version $MAJOR.$MINOR" ;;
    *)   p_fail "subsystem version $MAJOR.$MINOR is above XP's ceiling (5.1) -- link with -Wl,--subsystem,console:4.0"; FAILED=1 ;;
esac

# 3. Imported DLLs, against what XP ships.
IMPORTED_DLLS=$($OBJDUMP -p "$BIN" | awk '/DLL Name:/ {print toupper($3)}' | sed 's/\.DLL$//' | sort -u)
for dll in $IMPORTED_DLLS; do
    found=0
    for known in $XP_DLLS; do
        [ "$dll" = "$known" ] && { found=1; break; }
    done
    if [ "$found" = 1 ]; then
        p_ok "imports $dll (present on XP)"
    else
        p_fail "imports $dll -- not shipped with Windows XP"
        FAILED=1
    fi
done

# 4. Imported symbols, against the post-XP tripwire list.
SYMS=$($OBJDUMP -p "$BIN" | awk '$1 ~ /^[0-9a-f]+$/ && NF >= 3 {print $NF}' | sort -u)
for bad in $POST_XP_SYMS; do
    if printf '%s\n' "$SYMS" | grep -qx "$bad"; then
        p_fail "imports $bad -- added after Windows XP"
        FAILED=1
    fi
done
[ "$FAILED" = 0 ] && p_ok "no post-XP symbol among $(printf '%s\n' "$SYMS" | wc -l | tr -d ' ') imports"

# 5. The C runtime. mingw-w64's UCRT configuration links api-ms-win-crt-*,
#    which XP has no answer for; the msvcrt.dll configuration is the tier's.
if printf '%s\n' "$IMPORTED_DLLS" | grep -q '^MSVCRT$'; then
    p_ok "links msvcrt.dll (not UCRT)"
else
    p_fail "no msvcrt.dll import -- this toolchain is probably UCRT-configured, which XP cannot load"
    FAILED=1
fi

# 6. The TLS stack this tier cannot do without. XP's schannel has no TLS 1.2,
#    so the binary carries BearSSL and a CA bundle of its own (lib/tls.c): if
#    either is missing from the image, every HTTPS fetch on XP fails and the
#    checks above would not notice.
if printf '%s\n' "$IMPORTED_DLLS" | grep -q '^WS2_32$'; then
    p_ok "imports ws2_32 (the sockets lib/tls.c runs TLS over)"
else
    p_fail "no ws2_32 import -- lib/tls.c was not linked in, so HTTPS cannot work on XP"
    FAILED=1
fi
if grep -q "BEGIN CERTIFICATE" "$BIN"; then
    p_ok "carries the CA bundle"
else
    p_fail "no CA bundle in the image -- nothing to verify a certificate against"
    FAILED=1
fi

if [ "$FAILED" = 0 ]; then
    sec "XP tier: image is loadable on Windows XP"
    exit 0
fi
sec "XP tier: FAILED"
exit 1
