#!/bin/sh
# Integration test for the POSIX runtime C-module backend. It uses a registered
# module name with a temporary source tree, so no real installer action runs.
set -eu

ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
TMP=${TMPDIR:-/tmp}/osr-runtime-test.$$
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

mkdir -p "$TMP/lib" "$TMP/modules" "$TMP/home" "$TMP/cache"
cp "$ROOT/lib/common.h" "$ROOT/lib/module.h" "$TMP/lib/"

COUNT="$TMP/compiler.count"
WRAPPER="$TMP/compiler"
REAL_CC=$(command -v cc)
cat >"$WRAPPER" <<'EOF'
#!/bin/sh
printf 'compile\n' >>"$OSR_COMPILE_COUNT"
exec "$OSR_REAL_CC" "$@"
EOF
chmod +x "$WRAPPER"

write_module() {
    message=$1
    cat >"$TMP/modules/arandr.c" <<EOF
#include "../lib/module.h"
int osrm_arandr(void) {
    osr_infof("$message");
    return 1;
}
EOF
}

run_runtime() {
    env HOME="$TMP/home" OSR_HOME="$TMP/home" \
        XDG_CACHE_HOME="$TMP/cache" OSR_ROOT="$TMP" OSR_DOTFILES="$TMP" \
        OSR_MODULE_CC="$WRAPPER" OSR_COMPILE_COUNT="$COUNT" \
        OSR_REAL_CC="$REAL_CC" NO_COLOR=1 \
        "$ROOT/build/osr-runtime" module run arandr
}

compile_count() {
    if [ -f "$COUNT" ]; then
        wc -l <"$COUNT" | tr -d ' '
    else
        printf '0'
    fi
}

write_module runtime-first
run_runtime >"$TMP/out" 2>&1
grep -q 'runtime-first' "$TMP/out"
[ "$(compile_count)" = 1 ]

# Same source, headers, compiler and target must hit cache without invoking the
# compiler wrapper again.
run_runtime >"$TMP/out" 2>&1
grep -q 'runtime-first' "$TMP/out"
[ "$(compile_count)" = 1 ]

# Source content is part of the key, so changing it produces and loads a new
# object rather than reusing stale code.
write_module runtime-second
run_runtime >"$TMP/out" 2>&1
grep -q 'runtime-second' "$TMP/out"
[ "$(compile_count)" = 2 ]

# A successfully compiled object without the required entry point is a module
# failure. It must not fall through to any shell implementation.
cat >"$TMP/modules/arandr.c" <<'EOF'
#include "../lib/module.h"
int some_other_entry(void) { return 1; }
EOF
if run_runtime >"$TMP/out" 2>&1; then
    printf 'runtime module without entry point unexpectedly succeeded\n' >&2
    exit 1
fi
grep -q 'has no osrm_arandr entry point' "$TMP/out"
[ "$(compile_count)" = 3 ]

# Failed compilation must not publish a cache object.
write_module compile-failure
objects_before=$(find "$TMP/cache" -name '*.so' -type f | wc -l | tr -d ' ')
if env HOME="$TMP/home" OSR_HOME="$TMP/home" \
    XDG_CACHE_HOME="$TMP/cache" OSR_ROOT="$TMP" OSR_DOTFILES="$TMP" \
    OSR_MODULE_CC=false NO_COLOR=1 \
    "$ROOT/build/osr-runtime" module run arandr >"$TMP/out" 2>&1; then
    printf 'runtime module with failing compiler unexpectedly succeeded\n' >&2
    exit 1
fi
grep -q 'module compilation failed' "$TMP/out"
objects_after=$(find "$TMP/cache" -name '*.so' -type f | wc -l | tr -d ' ')
[ "$objects_before" = "$objects_after" ]

# Registry lookup rejects traversal before runtime source resolution.
if env OSR_ROOT="$TMP" "$ROOT/build/osr-runtime" module run ../arandr \
    >"$TMP/out" 2>&1; then
    printf 'runtime module traversal name unexpectedly succeeded\n' >&2
    exit 1
fi
grep -q 'no such C module' "$TMP/out"

# Full static output dispatches its linked object and ignores runtime compiler
# configuration entirely.
static_before=$(compile_count)
env HOME="$TMP/home" OSR_HOME="$TMP/home" OSR_ROOT="$TMP" \
    OSR_MODULE_CC="$WRAPPER" OSR_COMPILE_COUNT="$COUNT" \
    OSR_REAL_CC="$REAL_CC" NO_COLOR=1 \
    "$ROOT/build/osr" module run --theme-only arandr >"$TMP/out" 2>&1
[ "$(compile_count)" = "$static_before" ]

printf 'runtime module tests passed\n'
