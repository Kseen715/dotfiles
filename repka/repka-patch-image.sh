#!/usr/bin/env bash
# repka-patch-image.sh - bake the BSP pin into a Repka Pi 3 image, so every card
# you flash is protected from first boot and needs no post-install step.
#
# WHY: the stock image ships a custom Repka BSP versioned "26.08.0-trunk", while
# apt.armbian.com carries official generic sunxi64 packages of the same names at a
# HIGHER version (26.8.3) that contain no sun50i-h5-repka-pi3.dtb. `apt upgrade`
# installs those, the Repka device tree disappears, and U-Boot can no longer boot
# the board. See README.md.
#
# Works on .img and .img.xz. Needs NO root: the ext4 partition is carved out of the
# image, edited offline with debugfs, and written back.
#
# Usage:
#   ./repka-patch-image.sh Repka_26.08.0-trunk_Repkapi3_trixie_current_6.18.48.img.xz
#   ./repka-patch-image.sh -o pinned.img.xz --disable-armbian-repo input.img.xz
#
# Options:
#   -o, --output FILE       output path (default: <input>-pinned.img[.xz])
#       --xz / --no-xz      force compressed / raw output (default: match input)
#       --level N           xz compression level 0-9 (default 6)
#       --disable-armbian-repo  also disable apt.armbian.com inside the image
#       --force             overwrite an existing output file
#       --keep-tmp          keep the scratch dir (for debugging)
#   -h, --help

set -euo pipefail

# Packages that carry board-specific boot content and must never be replaced by
# the official Armbian sunxi64 builds.
BSP_PKGS=(
  linux-image-current-sunxi64
  linux-dtb-current-sunxi64
  linux-headers-current-sunxi64
  linux-u-boot-repkapi3-current
  armbian-bsp-cli-repkapi3-current
)

INPUT=""; OUTPUT=""; WANT_XZ=""; LEVEL=6; DISABLE_REPO=0; FORCE=0; KEEP_TMP=0
while [ $# -gt 0 ]; do
  case "$1" in
    -o|--output) shift; OUTPUT=${1:-} ;;
    --xz)        WANT_XZ=1 ;;
    --no-xz)     WANT_XZ=0 ;;
    --level)     shift; LEVEL=${1:-6} ;;
    --disable-armbian-repo) DISABLE_REPO=1 ;;
    --force)     FORCE=1 ;;
    --keep-tmp)  KEEP_TMP=1 ;;
    -h|--help)   awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "$0"; exit 0 ;;
    -*)          echo "unknown option: $1" >&2; exit 2 ;;
    *)           INPUT=$1 ;;
  esac
  shift
done

log()  { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[!]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[x]\033[0m %s\n' "$*" >&2; exit 1; }

[ -n "$INPUT" ] || die "no input image given (see --help)"
[ -r "$INPUT" ] || die "cannot read: $INPUT"
for t in xz dd debugfs e2fsck sfdisk awk; do
  command -v "$t" >/dev/null || die "missing required tool: $t"
done

# ------------------------------------------------------------ output naming
case "$INPUT" in
  *.img.xz) BASE=${INPUT%.img.xz}; IN_XZ=1 ;;
  *.img)    BASE=${INPUT%.img};    IN_XZ=0 ;;
  *) die "input must be .img or .img.xz" ;;
esac
[ -n "$WANT_XZ" ] || WANT_XZ=$IN_XZ
if [ -z "$OUTPUT" ]; then
  OUTPUT="${BASE}-pinned.img"; [ "$WANT_XZ" = 1 ] && OUTPUT="${OUTPUT}.xz"
fi
[ -e "$OUTPUT" ] && [ "$FORCE" != 1 ] && die "output exists: $OUTPUT (use --force)"

TMP=$(mktemp -d "${TMPDIR:-/var/tmp}/repka-patch.XXXXXX")
trap '[ "$KEEP_TMP" = 1 ] && echo "kept scratch: $TMP" || rm -rf "$TMP"' EXIT

# The patcher needs room for the decompressed image plus a copy of its root
# partition. Running out of space midway silently truncates files, so check
# first rather than discovering it in the middle of the dpkg database.
IN_BYTES=$(stat -c %s "$INPUT")
if [ "$IN_XZ" = 1 ]; then
  RAW_BYTES=$(xz --robot -l "$INPUT" 2>/dev/null | awk '$1=="totals"{print $5; exit}')
  [ -n "${RAW_BYTES:-}" ] && [ "$RAW_BYTES" -gt 0 ] 2>/dev/null || RAW_BYTES=$(( IN_BYTES * 4 ))
else
  RAW_BYTES=$IN_BYTES
fi
NEED_KB=$(( (RAW_BYTES * 2) / 1024 + 262144 ))    # image + partition copy + slack
AVAIL_KB=$(df -Pk "$TMP" | awk 'NR==2{print $4}')
if [ "$AVAIL_KB" -lt "$NEED_KB" ]; then
  die "not enough scratch space in $(dirname "$TMP"): need ~$(( NEED_KB / 1024 ))MiB, have $(( AVAIL_KB / 1024 ))MiB.
    Set TMPDIR to a roomier filesystem, e.g.  TMPDIR=/var/tmp $0 ..."
fi
log "Scratch: $(dirname "$TMP") ($(( AVAIL_KB / 1024 ))MiB free, need ~$(( NEED_KB / 1024 ))MiB)"

WORK="$TMP/work.img"
PART="$TMP/part.img"

# ------------------------------------------------------------- 1. decompress
if [ "$IN_XZ" = 1 ]; then
  log "Decompressing $(basename "$INPUT")..."
  xz -dc -T0 "$INPUT" > "$WORK"
else
  log "Copying $(basename "$INPUT")..."
  cp --reflink=auto "$INPUT" "$WORK"
fi
log "Working image: $(du -h "$WORK" | cut -f1)"

# --------------------------------------------------------- 2. partition table
PT=$(sfdisk -J "$WORK") || die "cannot read the partition table of the image"
SECSZ=$(awk -F: '/"sectorsize"[[:space:]]*:/ { gsub(/[^0-9]/,"",$2); print $2; exit }' <<<"$PT")
[ -n "$SECSZ" ] || SECSZ=512
# First partition only: the Repka image is a single ext4 rootfs with /boot inside.
read -r P_START P_SIZE < <(
  awk -F: '
    /"start"[[:space:]]*:/ { gsub(/[^0-9]/,"",$2); s=$2 }
    /"size"[[:space:]]*:/  { gsub(/[^0-9]/,"",$2); if (s != "" && !done) { print s, $2; done=1 } }
  ' <<<"$PT"
)
[ -n "${P_START:-}" ] && [ -n "${P_SIZE:-}" ] || die "could not read partition 1 geometry"
log "Partition 1: start sector $P_START, $P_SIZE sectors (${SECSZ}B each)"

# --------------------------------------------------------- 3. carve out rootfs
log "Extracting root filesystem for offline editing..."
dd if="$WORK" of="$PART" bs="$SECSZ" skip="$P_START" count="$P_SIZE" status=none

e2fsck -fp "$PART" >/dev/null 2>&1 || true   # 1 = errors fixed, both acceptable
BOARD=$(debugfs -R "cat /etc/armbian-release" "$PART" 2>/dev/null | sed -n 's/^BOARD=//p' | head -1)
[ -n "$BOARD" ] || die "partition 1 is not an Armbian rootfs (no /etc/armbian-release)"
[ "$BOARD" = "repkapi3" ] || die "image BOARD is '$BOARD', not repkapi3 - refusing to patch"
FDT=$(debugfs -R "cat /boot/armbianEnv.txt" "$PART" 2>/dev/null | sed -n 's/^fdtfile=//p' | head -1)
log "Image board: $BOARD, fdtfile: ${FDT:-<unset>}"
[ -n "$FDT" ] || die "no fdtfile= in /boot/armbianEnv.txt"
debugfs -R "stat /boot/dtb/$FDT" "$PART" >/dev/null 2>&1 \
  || die "image lacks /boot/dtb/$FDT - nothing to protect, wrong image?"

# ---------------------------------------------------------- 4. stage new files
mkdir -p "$TMP/stage"

{
  echo "# Managed by repka-patch-image.sh - do not edit by hand."
  echo "# Official Armbian sunxi64 packages outrank the custom Repka BSP by version"
  echo "# but ship no $FDT. Installing them makes the board unbootable."
  for p in "${BSP_PKGS[@]}"; do
    printf '\nPackage: %s\nPin: release o=Armbian\nPin-Priority: -1\n' "$p"
  done
} > "$TMP/stage/99-repka-bsp-pin"

cat > "$TMP/stage/repka-boot-check" <<'CHK'
#!/usr/bin/env bash
# Verifies the pieces U-Boot needs are all present. Baked in by repka-patch-image.sh.
set -uo pipefail
rc=0
say() { printf '\033[1;31m[repka-boot-check] %s\033[0m\n' "$*" >&2; rc=1; }

fdt=$(sed -n 's/^fdtfile=//p' /boot/armbianEnv.txt 2>/dev/null | head -1)
[ -n "$fdt" ] || say "no fdtfile= in /boot/armbianEnv.txt"
[ -n "$fdt" ] && [ ! -e "/boot/dtb/$fdt" ] && say "MISSING DTB /boot/dtb/$fdt - THIS BOARD WILL NOT BOOT"
[ -e /boot/Image ]    || say "MISSING /boot/Image"
[ -e /boot/uInitrd ]  || say "MISSING /boot/uInitrd"
[ -e /boot/boot.scr ] || say "MISSING /boot/boot.scr"

if [ "$rc" != 0 ]; then
  echo "[repka-boot-check] DO NOT REBOOT. Restore the Repka BSP first." >&2
else
  [ "${1:-}" = --quiet ] || echo "[repka-boot-check] boot files OK (dtb=$fdt)"
fi
exit $rc
CHK

cat > "$TMP/stage/99-repka-boot-check" <<'HOOK'
// Baked in by repka-patch-image.sh - shout loudly if an apt run breaks the boot set.
DPkg::Post-Invoke { "if [ -x /usr/local/sbin/repka-boot-check ]; then /usr/local/sbin/repka-boot-check || true; fi"; };
HOOK

# ------------------------------------------------------- 5. write into the fs
# debugfs `write` fails if the target exists, so unlink first; then fix ownership
# and mode, which `write` inherits from the invoking user, not from root.
fs_put() {  # local_file  target_path  octal_mode
  local src=$1 dst=$2 mode=$3 dir base
  dir=$(dirname "$dst"); base=$(basename "$dst")

  # Create the parent directory if the image lacks it.
  if ! debugfs -R "stat $dir" "$PART" >/dev/null 2>&1; then
    debugfs -w -f /dev/stdin "$PART" >/dev/null 2>&1 <<EOF
mkdir $dir
sif $dir mode 040755
sif $dir uid 0
sif $dir gid 0
EOF
  fi

  # debugfs `write` refuses to clobber, so unlink first. Then fix ownership and
  # mode: `write` copies them from the invoking user, not from root.
  debugfs -w -f /dev/stdin "$PART" >/dev/null 2>&1 <<EOF
cd $dir
rm $base
write $src $base
sif $base mode $mode
sif $base uid 0
sif $base gid 0
EOF

  debugfs -R "stat $dst" "$PART" >/dev/null 2>&1 || die "failed to write $dst into the image"
}

log "Installing apt pin, boot check and apt hook into the image"
fs_put "$TMP/stage/99-repka-bsp-pin"   /etc/apt/preferences.d/99-repka-bsp-pin 0100644
fs_put "$TMP/stage/repka-boot-check"   /usr/local/sbin/repka-boot-check        0100755
fs_put "$TMP/stage/99-repka-boot-check" /etc/apt/apt.conf.d/99-repka-boot-check 0100644

# ------------------------------------------------------------ 6. dpkg holds
log "Marking BSP packages 'hold' in the dpkg database"
debugfs -R "dump /var/lib/dpkg/status $TMP/status" "$PART" >/dev/null 2>&1 \
  || die "could not read /var/lib/dpkg/status from the image"

# `next` on each match matters: without it the rewritten line would fall through
# and be counted again by the already-held rule.
awk -v pkgs="${BSP_PKGS[*]}" '
  BEGIN { n = split(pkgs, a, " "); for (i = 1; i <= n; i++) want[a[i]] = 1 }
  /^Package: / { hit = ($2 in want) }
  hit && /^Status: install ok installed$/ { $0 = "Status: hold ok installed"; held++; print; next }
  hit && /^Status: hold ok installed$/    { already++; print; next }
  { print }
  END { print held + 0, already + 0 > "/dev/stderr" }
' "$TMP/status" > "$TMP/status.new" 2> "$TMP/heldcount"

read -r NEWLY_HELD ALREADY_HELD < "$TMP/heldcount"
HELD=$(( NEWLY_HELD + ALREADY_HELD ))
[ "$HELD" -gt 0 ] || die "no BSP packages found in the dpkg status file - wrong image?"

# A short write here would corrupt the package database. The rewrite only edits
# Status: lines in place, so the line count must be identical.
OLD_LINES=$(wc -l < "$TMP/status")
NEW_LINES=$(wc -l < "$TMP/status.new")
[ "$OLD_LINES" = "$NEW_LINES" ] \
  || die "dpkg status rewrite changed line count ($OLD_LINES -> $NEW_LINES) - out of disk space?"

if [ "$ALREADY_HELD" -gt 0 ]; then
  log "Held $HELD package(s): $NEWLY_HELD newly, $ALREADY_HELD already held (image was patched before)"
else
  log "Held $HELD package(s) in /var/lib/dpkg/status ($NEW_LINES lines, unchanged)"
fi

debugfs -w -f /dev/stdin "$PART" >/dev/null 2>&1 <<EOF
cd /var/lib/dpkg
rm status
write $TMP/status.new status
sif status mode 0100644
sif status uid 0
sif status gid 0
EOF

# ------------------------------------------------- 7. optionally kill the repo
if [ "$DISABLE_REPO" = 1 ]; then
  if debugfs -R "cat /etc/apt/sources.list.d/armbian.sources" "$PART" > "$TMP/armbian.sources" 2>/dev/null \
     && [ -s "$TMP/armbian.sources" ]; then
    # "Enabled: no" must be a FIELD of each stanza. A blank line terminates a
    # stanza, so appending after a trailing blank line (or to a file with no
    # final newline) would produce an orphan stanza that apt refuses to parse.
    awk '
      function flush(   i) {
        if (n == 0) return
        if (printed) print ""
        for (i = 1; i <= n; i++) print buf[i]
        print "Enabled: no"
        printed = 1
        n = 0
      }
      /^[[:space:]]*$/ { flush(); next }
      /^[Ee]nabled:/   { next }
      { buf[++n] = $0 }
      END { flush() }
    ' "$TMP/armbian.sources" > "$TMP/armbian.sources.new"
    [ -s "$TMP/armbian.sources.new" ] || die "rewriting armbian.sources produced an empty file"
    grep -q '^Enabled: no$' "$TMP/armbian.sources.new" || die "failed to disable armbian.sources"
    fs_put "$TMP/armbian.sources.new" /etc/apt/sources.list.d/armbian.sources 0100644
    warn "apt.armbian.com disabled inside the image"
  else
    warn "armbian.sources not found in the image - skipping --disable-armbian-repo"
  fi
fi

# ---------------------------------------------------------------- 8. verify
log "Verifying the patched filesystem..."
e2fsck -fp "$PART" >/dev/null 2>&1 || true

FAIL=0

# Read each file out of the image ONCE into a scratch file before grepping it.
# Piping debugfs straight into `grep -q` makes grep exit on first match, debugfs
# take SIGPIPE, and `set -o pipefail` report the whole check as failed - which
# only bites on files big enough that grep finishes first.
fs_cat() { debugfs -R "cat $1" "$PART" > "$2" 2>/dev/null || true; }

check_file() {  # path  needle  description
  fs_cat "$1" "$TMP/vcat"
  if grep -q -- "$2" "$TMP/vcat"; then
    printf '      \033[1;32mok\033[0m   %s\n' "$3"
  else
    printf '      \033[1;31mFAIL\033[0m %s\n' "$3"; FAIL=1
  fi
}
check_file /etc/apt/preferences.d/99-repka-bsp-pin  "Pin-Priority: -1"  "apt pin present"
check_file /etc/apt/apt.conf.d/99-repka-boot-check  "DPkg::Post-Invoke" "apt hook present"
check_file /usr/local/sbin/repka-boot-check         "repka-boot-check"  "boot check script present"

# Pull the package database out once and check every hold against it.
fs_cat /var/lib/dpkg/status "$TMP/verify-status"
if grep -q "^Status: hold ok installed" "$TMP/verify-status"; then
  printf '      \033[1;32mok\033[0m   dpkg holds present\n'
else
  printf '      \033[1;31mFAIL\033[0m dpkg holds present\n'; FAIL=1
fi

# It must have landed whole, not truncated: compare against what we staged.
if cmp -s "$TMP/status.new" "$TMP/verify-status"; then
  printf '      \033[1;32mok\033[0m   dpkg status intact (%s bytes)\n' "$(stat -c %s "$TMP/verify-status")"
else
  printf '      \033[1;31mFAIL\033[0m dpkg status in the image differs from what was staged\n'; FAIL=1
fi

MODE=$(debugfs -R "stat /usr/local/sbin/repka-boot-check" "$PART" 2>/dev/null | sed -n 's/.*Mode: *0*\([0-7]\{3,4\}\).*/\1/p' | head -1)
if [ "${MODE:-}" = "0755" ] || [ "${MODE:-}" = "755" ]; then
  printf '      \033[1;32mok\033[0m   boot check is executable (mode %s)\n' "$MODE"
else
  printf '      \033[1;31mFAIL\033[0m boot check mode is %s, expected 0755\n' "${MODE:-?}"; FAIL=1
fi

# Every BSP package that exists in the image must actually be marked hold.
for p in "${BSP_PKGS[@]}"; do
  ST=$(awk -v want="$p" '/^Package: /{c=($2==want)} c&&/^Status: /{print; exit}' "$TMP/verify-status")
  if [ -z "$ST" ]; then
    printf '      \033[1;33m--\033[0m   %s not in this image, skipped\n' "$p"
  elif [ "$ST" = "Status: hold ok installed" ]; then
    printf '      \033[1;32mok\033[0m   hold: %s\n' "$p"
  else
    printf '      \033[1;31mFAIL\033[0m %s is "%s", expected hold\n' "$p" "$ST"; FAIL=1
  fi
done

# The whole point: the Repka DTB must still be there.
if debugfs -R "stat /boot/dtb/$FDT" "$PART" >/dev/null 2>&1; then
  printf '      \033[1;32mok\033[0m   %s intact\n' "$FDT"
else
  printf '      \033[1;31mFAIL\033[0m %s went missing\n' "$FDT"; FAIL=1
fi

[ "$FAIL" = 0 ] || die "verification failed - output NOT written"

# ------------------------------------------------------- 9. reassemble output
log "Writing patched partition back into the image"
dd if="$PART" of="$WORK" bs="$SECSZ" seek="$P_START" count="$P_SIZE" conv=notrunc status=none

if [ "$WANT_XZ" = 1 ]; then
  log "Compressing to $(basename "$OUTPUT") (xz -$LEVEL -T0, this is the slow part)..."
  xz -c -"$LEVEL" -T0 "$WORK" > "$OUTPUT"
else
  log "Writing $(basename "$OUTPUT")"
  cp --reflink=auto "$WORK" "$OUTPUT"
fi

log "Done: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
echo
echo "  Flash it as usual. On first boot the board already has:"
echo "    - $HELD BSP packages on hold"
echo "    - an apt pin blocking the official Armbian sunxi64 packages"
echo "    - /usr/local/sbin/repka-boot-check wired into every apt run"
echo "  'apt update && apt full-upgrade' is safe out of the box."
