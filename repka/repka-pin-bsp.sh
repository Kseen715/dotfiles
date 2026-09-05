#!/usr/bin/env bash
# repka-pin-bsp.sh - make `apt upgrade` safe on a Repka Pi 3 running a custom
# (user-built) Armbian image.
#
# WHY: the image ships a Repka-specific BSP (kernel + sun50i-h5-repka-pi3.dtb)
# versioned "26.08.0-trunk". apt.armbian.com carries *official* sunxi64 packages
# with a higher version (e.g. 26.8.3) that contain NO Repka DTB. Upgrading swaps
# the kernel/DTB set, armbianEnv.txt still points at the missing DTB, and U-Boot
# has nothing to load -> the board never boots again.
#
# This script freezes the board-specific packages and leaves everything else
# (Debian security updates, userland) free to upgrade. Idempotent.
#
# Usage:  sudo ./repka-pin-bsp.sh [--dry-run] [--force] [--disable-armbian-repo]

set -euo pipefail

PIN_FILE=/etc/apt/preferences.d/99-repka-bsp-pin
HOOK_FILE=/etc/apt/apt.conf.d/99-repka-boot-check
CHECK_BIN=/usr/local/sbin/repka-boot-check
ARMBIAN_SOURCES=/etc/apt/sources.list.d/armbian.sources

DRY_RUN=0
FORCE=0
DISABLE_REPO=0

for arg in "$@"; do
  case "$arg" in
    --dry-run)              DRY_RUN=1 ;;
    --force)                FORCE=1 ;;
    --disable-armbian-repo) DISABLE_REPO=1 ;;
    -h|--help) awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "$0"; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

log()  { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[!]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[x]\033[0m %s\n' "$*" >&2; exit 1; }
run()  { if [ "$DRY_RUN" = 1 ]; then echo "  (dry-run) $*"; else "$@"; fi; }

[ "$(id -u)" -eq 0 ] || die "must run as root"

# ---------------------------------------------------------------- board check
[ -r /etc/armbian-release ] || die "/etc/armbian-release missing - not an Armbian system"
# shellcheck disable=SC1091
. /etc/armbian-release
log "Board: ${BOARD:-?} (${BOARD_NAME:-?}), family ${LINUXFAMILY:-?}, image type ${IMAGE_TYPE:-?}"

if [ "${BOARD:-}" != "repkapi3" ] && [ "$FORCE" != 1 ]; then
  die "BOARD is '${BOARD:-}' not 'repkapi3'. Re-run with --force if you know what you are doing."
fi

# ------------------------------------------------------- current boot inventory
FDTFILE=$(sed -n 's/^fdtfile=//p' /boot/armbianEnv.txt 2>/dev/null | head -1)
[ -n "$FDTFILE" ] || die "no fdtfile= in /boot/armbianEnv.txt"
log "armbianEnv fdtfile: $FDTFILE"

[ -e "/boot/dtb/$FDTFILE" ] || die "/boot/dtb/$FDTFILE is ALREADY missing - this board is broken, restore it first"
log "DTB present: /boot/dtb/$FDTFILE"

DTB_PKG=$(dpkg -S "$(readlink -f "/boot/dtb/$FDTFILE")" 2>/dev/null | cut -d: -f1 || true)
log "DTB provided by package: ${DTB_PKG:-<unpackaged>}"

# -------------------------------------------------------------- packages to pin
# Everything that carries board-specific boot content. Only act on installed ones.
CANDIDATES=(
  linux-image-current-sunxi64
  linux-dtb-current-sunxi64
  linux-headers-current-sunxi64
  linux-image-edge-sunxi64
  linux-dtb-edge-sunxi64
  linux-u-boot-repkapi3-current
  linux-u-boot-repkapi3-edge
  armbian-bsp-cli-repkapi3-current
  armbian-bsp-cli-repkapi3-edge
)
[ -n "$DTB_PKG" ] && CANDIDATES+=("$DTB_PKG")

INSTALLED=()
for p in "${CANDIDATES[@]}"; do
  if dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -qE '^(install|hold) ok installed'; then
    case " ${INSTALLED[*]-} " in *" $p "*) ;; *) INSTALLED+=("$p") ;; esac
  fi
done
[ "${#INSTALLED[@]}" -gt 0 ] || die "none of the expected BSP packages are installed"

log "Freezing ${#INSTALLED[@]} BSP package(s):"
for p in "${INSTALLED[@]}"; do
  printf '      %-34s %s\n' "$p" "$(dpkg-query -W -f='${Version}' "$p")"
done

# ---------------------------------------------------------------- 1. apt-mark
if [ "$DRY_RUN" = 1 ]; then
  echo "  (dry-run) apt-mark hold ${INSTALLED[*]}"
else
  apt-mark hold "${INSTALLED[@]}" >/dev/null
fi
log "apt-mark hold applied"

# --------------------------------------------------- 2. pin against o=Armbian
# Belt and braces: a hold can be overridden by --allow-change-held-packages,
# a negative pin cannot be reached by the resolver at all.
PIN_BODY="# Managed by repka-pin-bsp.sh - do not edit by hand.
# The Repka Pi 3 BSP is a custom build; official Armbian sunxi64 packages have a
# HIGHER version but ship no sun50i-h5-repka-pi3.dtb. Installing them bricks boot.
"
for p in "${INSTALLED[@]}"; do
  PIN_BODY+="
Package: $p
Pin: release o=Armbian
Pin-Priority: -1
"
done

if [ "$DRY_RUN" = 1 ]; then
  echo "  (dry-run) would write $PIN_FILE"
else
  printf '%s' "$PIN_BODY" > "$PIN_FILE"
  chmod 0644 "$PIN_FILE"
fi
log "Pin file written: $PIN_FILE"

# ------------------------------------------------- 3. boot sanity check + hook
read -r -d '' CHECK_SRC <<'CHK' || true
#!/usr/bin/env bash
# Verifies the pieces U-Boot needs are all present. Installed by repka-pin-bsp.sh.
set -uo pipefail
rc=0
say() { printf '\033[1;31m[repka-boot-check] %s\033[0m\n' "$*" >&2; rc=1; }

fdt=$(sed -n 's/^fdtfile=//p' /boot/armbianEnv.txt 2>/dev/null | head -1)
[ -n "$fdt" ] || say "no fdtfile= in /boot/armbianEnv.txt"
[ -n "$fdt" ] && [ ! -e "/boot/dtb/$fdt" ] && say "MISSING DTB /boot/dtb/$fdt - THIS BOARD WILL NOT BOOT"
[ -e /boot/Image ]   || say "MISSING /boot/Image"
[ -e /boot/uInitrd ] || say "MISSING /boot/uInitrd"
[ -e /boot/boot.scr ] || say "MISSING /boot/boot.scr"

if [ "$rc" != 0 ]; then
  echo "[repka-boot-check] DO NOT REBOOT. Restore the Repka BSP first." >&2
else
  [ "${1:-}" = --quiet ] || echo "[repka-boot-check] boot files OK (dtb=$fdt)"
fi
exit $rc
CHK

if [ "$DRY_RUN" = 1 ]; then
  echo "  (dry-run) would install $CHECK_BIN and $HOOK_FILE"
else
  printf '%s\n' "$CHECK_SRC" > "$CHECK_BIN"
  chmod 0755 "$CHECK_BIN"
  cat > "$HOOK_FILE" <<HOOK
// Managed by repka-pin-bsp.sh - shout loudly if an apt run breaks the boot set.
DPkg::Post-Invoke { "if [ -x $CHECK_BIN ]; then $CHECK_BIN || true; fi"; };
HOOK
  chmod 0644 "$HOOK_FILE"
fi
log "Boot sanity check installed: $CHECK_BIN (runs after every apt operation)"

# ------------------------------------------------- 4. optionally kill the repo
# Append "Enabled: no" as a FIELD of every stanza in a deb822 sources file.
# A blank line terminates a stanza, so the field must sit INSIDE the stanza with
# no blank line before it - otherwise apt sees an orphan stanza and refuses to
# parse the file at all, breaking every apt command on the board.
deb822_disable() {
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
  '
}

if [ "$DISABLE_REPO" = 1 ] && [ -f "$ARMBIAN_SOURCES" ]; then
  if [ "$DRY_RUN" = 1 ]; then
    echo "  (dry-run) would disable $ARMBIAN_SOURCES"
  else
    BAK="${ARMBIAN_SOURCES}.repka-bak"
    cp -a "$ARMBIAN_SOURCES" "$BAK"
    deb822_disable < "$BAK" > "${ARMBIAN_SOURCES}.tmp"
    chmod --reference="$BAK" "${ARMBIAN_SOURCES}.tmp" 2>/dev/null || chmod 0644 "${ARMBIAN_SOURCES}.tmp"
    mv "${ARMBIAN_SOURCES}.tmp" "$ARMBIAN_SOURCES"

    # A malformed sources file breaks apt entirely, so prove it still parses and
    # roll back if it does not.
    if ! apt-get indextargets >/dev/null 2>&1; then
      mv "$BAK" "$ARMBIAN_SOURCES"
      die "disabling $ARMBIAN_SOURCES made apt unable to parse it - reverted, board untouched"
    fi
    rm -f "$BAK"
  fi
  warn "apt.armbian.com disabled entirely (--disable-armbian-repo)"
fi

# ------------------------------------------------------------------ 5. verify
log "Refreshing package lists and re-simulating the upgrade..."
if [ "$DRY_RUN" = 1 ]; then
  echo "  (dry-run) skipping verification"
  exit 0
fi

apt-get update -qq 2>/dev/null || warn "apt-get update reported problems"
SIM=$(apt-get -s full-upgrade 2>/dev/null || true)

BAD=0
for p in "${INSTALLED[@]}"; do
  if grep -Eq "^Inst $p " <<<"$SIM"; then
    warn "STILL WOULD UPGRADE: $p"
    BAD=1
  fi
done

if [ "$BAD" != 0 ]; then
  die "pinning did not take effect - do NOT run apt upgrade on this board"
fi

log "Verified: no BSP package would be touched by 'apt full-upgrade'."
echo
grep -E '^[0-9]+ upgraded' <<<"$SIM" || true
echo
"$CHECK_BIN"
log "Done. 'apt update && apt full-upgrade' is now safe on this board."
