#!/usr/bin/env bash
# repka-recover-sd.sh - repair a Repka Pi 3 SD card that was bricked by
# `apt upgrade` replacing the custom BSP with official Armbian sunxi64 packages.
#
# Run on your workstation with the SD card inserted. It restores /boot (kernel,
# DTBs, initrd, boot script) from the pristine .img.xz onto the card's existing
# rootfs, so you keep the card's data and its root UUID, then re-applies the
# pin/hold so it cannot happen again.
#
# Usage:
#   sudo ./repka-recover-sd.sh --image Repka_26.08.0-...img.xz --device /dev/sdX
#
# Options:
#   --image FILE     pristine Repka image (.img.xz or .img)
#   --device DEV     the SD card block device (e.g. /dev/sdb, /dev/mmcblk0)
#   --uboot          also restore the raw U-Boot area (only if U-Boot itself is
#                    suspect; refused if it would overlap partition 1)
#   --yes            skip the interactive confirmation
#   --keep-tmp       do not delete the decompressed image (speeds up repeat runs)

set -euo pipefail

IMAGE=""; DEVICE=""; ASSUME_YES=0; DO_UBOOT=0; KEEP_TMP=0
while [ $# -gt 0 ]; do
  case "$1" in
    --image)  shift; IMAGE=${1:-} ;;
    --device) shift; DEVICE=${1:-} ;;
    --uboot)  DO_UBOOT=1 ;;
    --yes)    ASSUME_YES=1 ;;
    --keep-tmp) KEEP_TMP=1 ;;
    -h|--help) awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

log()  { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[!]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[x]\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "must run as root"
[ -n "$IMAGE" ]  || die "--image is required"
[ -n "$DEVICE" ] || die "--device is required"
[ -r "$IMAGE" ]  || die "cannot read image: $IMAGE"
[ -b "$DEVICE" ] || die "not a block device: $DEVICE"

for t in losetup xz partx blkid rsync; do command -v "$t" >/dev/null || die "missing tool: $t"; done

# ------------------------------------------------------------- safety checks
ROOT_SRC=$(findmnt -no SOURCE / || true)
ROOT_DISK=$(lsblk -no PKNAME "$ROOT_SRC" 2>/dev/null || true)
DEV_NAME=$(basename "$DEVICE")
[ -n "$ROOT_DISK" ] && [ "$ROOT_DISK" = "$DEV_NAME" ] && die "$DEVICE hosts your running root filesystem - refusing"

if lsblk -no MOUNTPOINT "$DEVICE" | grep -qE '^/(|boot|home)$'; then
  die "$DEVICE has partitions mounted at critical paths - refusing"
fi

RM=$(lsblk -dno RM "$DEVICE" | tr -d ' ')
SIZE=$(lsblk -dno SIZE "$DEVICE" | tr -d ' ')
MODEL=$(lsblk -dno MODEL "$DEVICE" | sed 's/ *$//')
[ "$RM" = "1" ] || warn "$DEVICE is not flagged removable - double-check this is the SD card"

echo
echo "  Image : $IMAGE"
echo "  Target: $DEVICE  ($SIZE${MODEL:+, $MODEL})"
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINT "$DEVICE"
echo
if [ "$ASSUME_YES" != 1 ]; then
  read -rp "Restore the Repka /boot BSP onto ${DEVICE}p*/1? Type YES to continue: " ans
  [ "$ans" = "YES" ] || die "aborted"
fi

# ------------------------------------------------------------------ scratch
TMP=$(mktemp -d /var/tmp/repka-recover.XXXXXX)
IMG_MNT="$TMP/img"; CARD_MNT="$TMP/card"
mkdir -p "$IMG_MNT" "$CARD_MNT"
LOOP=""
cleanup() {
  set +e
  mountpoint -q "$CARD_MNT" && umount "$CARD_MNT"
  mountpoint -q "$IMG_MNT"  && umount "$IMG_MNT"
  [ -n "$LOOP" ] && losetup -d "$LOOP"
  if [ "$KEEP_TMP" = 1 ]; then echo "kept: $TMP"; else rm -rf "$TMP"; fi
}
trap cleanup EXIT

# ------------------------------------------------------ decompress + attach
case "$IMAGE" in
  *.xz)
    RAW="$TMP/image.img"
    log "Decompressing $(basename "$IMAGE") (this takes a minute)..."
    xz -dc "$IMAGE" > "$RAW"
    ;;
  *) RAW="$IMAGE" ;;
esac

log "Attaching image to a loop device"
LOOP=$(losetup -f -P --show -r "$RAW")
IMG_P1="${LOOP}p1"
[ -b "$IMG_P1" ] || die "image has no partition 1 ($IMG_P1)"
mount -o ro "$IMG_P1" "$IMG_MNT"
[ -d "$IMG_MNT/boot" ] || die "image partition 1 has no /boot - unexpected layout"

IMG_FDT=$(sed -n 's/^fdtfile=//p' "$IMG_MNT/boot/armbianEnv.txt" | head -1)
IMG_KVER=$(basename "$(readlink -f "$IMG_MNT/boot/Image")" | sed 's/^vmlinuz-//')
log "Image BSP: kernel $IMG_KVER, fdt $IMG_FDT"
[ -e "$IMG_MNT/boot/dtb/$IMG_FDT" ] || die "image itself lacks $IMG_FDT - wrong image?"

# -------------------------------------------------------------- mount card
CARD_P1="${DEVICE}p1"; [ -b "$CARD_P1" ] || CARD_P1="${DEVICE}1"
[ -b "$CARD_P1" ] || die "cannot find partition 1 on $DEVICE"
mountpoint -q "$CARD_MNT" || mount "$CARD_P1" "$CARD_MNT"

[ -r "$CARD_MNT/etc/armbian-release" ] || die "$CARD_P1 does not look like an Armbian rootfs"
CARD_BOARD=$(sed -n 's/^BOARD=//p' "$CARD_MNT/etc/armbian-release" | head -1)
log "Card board: $CARD_BOARD"
[ "$CARD_BOARD" = "repkapi3" ] || die "card BOARD is '$CARD_BOARD', not repkapi3 - refusing"

CARD_FDT=$(sed -n 's/^fdtfile=//p' "$CARD_MNT/boot/armbianEnv.txt" 2>/dev/null | head -1)
if [ -e "$CARD_MNT/boot/dtb/$CARD_FDT" ]; then
  warn "card already has $CARD_FDT - it may not be the DTB that is broken"
else
  log "Confirmed damage: /boot/dtb/$CARD_FDT is missing on the card"
fi

# ------------------------------------------------------------ restore /boot
log "Backing up card /boot to /boot.broken-$(date +%Y%m%d%H%M%S)"
cp -a "$CARD_MNT/boot" "$CARD_MNT/boot.broken-$(date +%Y%m%d%H%M%S)"

log "Restoring BSP files from image"
rsync -aH --info=stats1 \
  --exclude 'armbianEnv.txt' \
  "$IMG_MNT/boot/" "$CARD_MNT/boot/"

# Keep the card's own armbianEnv.txt (correct root UUID) but force the fdtfile
# back to the image's value in case the upgrade rewrote it.
if [ -n "$IMG_FDT" ] && [ -w "$CARD_MNT/boot/armbianEnv.txt" ]; then
  sed -i "s|^fdtfile=.*|fdtfile=$IMG_FDT|" "$CARD_MNT/boot/armbianEnv.txt"
fi

# Repoint the symlinks at the restored kernel.
ln -sfn "vmlinuz-$IMG_KVER" "$CARD_MNT/boot/Image"
ln -sfn "dtb-$IMG_KVER"     "$CARD_MNT/boot/dtb"
[ -e "$CARD_MNT/boot/uInitrd-$IMG_KVER" ] && ln -sfn "uInitrd-$IMG_KVER" "$CARD_MNT/boot/uInitrd"

[ -e "$CARD_MNT/boot/dtb/$IMG_FDT" ] || die "restore failed: $IMG_FDT still missing"
log "Restored: kernel $IMG_KVER, dtb $IMG_FDT"

# --------------------------------------------------- optional raw U-Boot area
if [ "$DO_UBOOT" = 1 ]; then
  P1_START=$(partx -g -o START "$CARD_P1" | tr -d ' ')   # in 512b sectors
  MAX_BYTES=$(( P1_START * 512 ))
  if [ "$MAX_BYTES" -le $(( 8 * 1024 )) ]; then
    warn "partition 1 starts at ${MAX_BYTES}B - no room for U-Boot, skipping"
  else
    COUNT_K=$(( (MAX_BYTES - 8*1024) / 1024 ))
    log "Restoring raw U-Boot: 8KiB..$(( MAX_BYTES / 1024 ))KiB (${COUNT_K}KiB)"
    dd if="$RAW" of="$DEVICE" bs=1024 skip=8 seek=8 count="$COUNT_K" conv=notrunc status=none
  fi
fi

# ------------------------------------------------ re-apply the pin on the card
log "Pre-applying the BSP pin so the card cannot re-break"
cat > "$CARD_MNT/etc/apt/preferences.d/99-repka-bsp-pin" <<'PIN'
# Managed by repka-recover-sd.sh - do not edit by hand.
# Official Armbian sunxi64 packages outrank the custom Repka BSP by version but
# ship no sun50i-h5-repka-pi3.dtb. Installing them makes the board unbootable.

Package: linux-image-current-sunxi64
Pin: release o=Armbian
Pin-Priority: -1

Package: linux-dtb-current-sunxi64
Pin: release o=Armbian
Pin-Priority: -1

Package: linux-headers-current-sunxi64
Pin: release o=Armbian
Pin-Priority: -1

Package: linux-u-boot-repkapi3-current
Pin: release o=Armbian
Pin-Priority: -1

Package: armbian-bsp-cli-repkapi3-current
Pin: release o=Armbian
Pin-Priority: -1
PIN

# dpkg holds live in the selections DB; set them offline.
for p in linux-image-current-sunxi64 linux-dtb-current-sunxi64 \
         linux-u-boot-repkapi3-current armbian-bsp-cli-repkapi3-current; do
  printf '%s hold\n' "$p"
done | chroot "$CARD_MNT" dpkg --set-selections 2>/dev/null \
  || warn "could not set dpkg holds offline (no arm64 emulation?) - run repka-pin-bsp.sh on first boot"

sync
log "Done. Eject the card, boot the board, then run repka-pin-bsp.sh to verify."
