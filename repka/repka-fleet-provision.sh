#!/usr/bin/env bash
# repka-fleet-provision.sh - push repka-pin-bsp.sh to N freshly-flashed Repka Pi 3
# boards over SSH, then (optionally) run the now-safe upgrade on each.
#
# Usage:
#   ./repka-fleet-provision.sh rpi-glr-02 rpi-glr-03 ...
#   ./repka-fleet-provision.sh --hosts hosts.txt          # one host per line, # comments ok
#   ./repka-fleet-provision.sh --upgrade --reboot rpi-glr-02
#
# Options:
#   --hosts FILE            read hosts from FILE instead of argv
#   --upgrade               run `apt full-upgrade` after pinning
#   --reboot                reboot after upgrading and wait for the host to return
#   --dry-run               pass --dry-run through to the pin script
#   --disable-armbian-repo  pass through: turn apt.armbian.com off entirely
#
# Every host is independent; a failure on one is reported and the run continues.

set -uo pipefail

HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PIN_SCRIPT="$HERE/repka-pin-bsp.sh"
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new)

HOSTS=()
DO_UPGRADE=0
DO_REBOOT=0
PIN_ARGS=()

while [ $# -gt 0 ]; do
  case "$1" in
    --hosts)   shift; [ -r "${1:-}" ] || { echo "cannot read hosts file: ${1:-}" >&2; exit 2; }
               while read -r l; do l="${l%%#*}"; l="${l// /}"; [ -n "$l" ] && HOSTS+=("$l"); done < "$1" ;;
    --upgrade) DO_UPGRADE=1 ;;
    --reboot)  DO_REBOOT=1 ;;
    --dry-run|--disable-armbian-repo|--force) PIN_ARGS+=("$1") ;;
    -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
    -*)        echo "unknown option: $1" >&2; exit 2 ;;
    *)         HOSTS+=("$1") ;;
  esac
  shift
done

[ -r "$PIN_SCRIPT" ] || { echo "missing $PIN_SCRIPT" >&2; exit 1; }
[ "${#HOSTS[@]}" -gt 0 ] || { echo "no hosts given (see --help)" >&2; exit 2; }

hdr()  { printf '\n\033[1;36m######## %s\033[0m\n' "$*"; }
ok()   { printf '\033[1;32m==> %s\033[0m\n' "$*"; }
bad()  { printf '\033[1;31m[x] %s\033[0m\n' "$*" >&2; }

wait_for_ssh() {  # host, timeout_seconds
  local h=$1 limit=${2:-240} start
  start=$(date +%s)
  while [ $(( $(date +%s) - start )) -lt "$limit" ]; do
    if ssh "${SSH_OPTS[@]}" "$h" true 2>/dev/null; then return 0; fi
    sleep 5
  done
  return 1
}

FAILED=()
for h in "${HOSTS[@]}"; do
  hdr "$h"

  if ! ssh "${SSH_OPTS[@]}" "$h" true 2>/dev/null; then
    bad "$h: unreachable over SSH"; FAILED+=("$h"); continue
  fi

  if ! scp -q "${SSH_OPTS[@]}" "$PIN_SCRIPT" "$h:/tmp/repka-pin-bsp.sh"; then
    bad "$h: scp failed"; FAILED+=("$h"); continue
  fi

  if ! ssh "${SSH_OPTS[@]}" "$h" "sudo -n bash /tmp/repka-pin-bsp.sh ${PIN_ARGS[*]-}"; then
    bad "$h: pinning FAILED - do not upgrade this board"; FAILED+=("$h"); continue
  fi
  ok "$h: BSP pinned"

  if [ "$DO_UPGRADE" = 1 ]; then
    if ! ssh "${SSH_OPTS[@]}" "$h" \
        'sudo -n env DEBIAN_FRONTEND=noninteractive apt-get -y -o Dpkg::Options::=--force-confold full-upgrade'; then
      bad "$h: upgrade failed"; FAILED+=("$h"); continue
    fi
    ok "$h: upgraded"

    # The post-invoke hook already ran; assert explicitly before we dare reboot.
    if ! ssh "${SSH_OPTS[@]}" "$h" 'sudo -n /usr/local/sbin/repka-boot-check'; then
      bad "$h: BOOT FILES BROKEN AFTER UPGRADE - not rebooting"; FAILED+=("$h"); continue
    fi
  fi

  if [ "$DO_REBOOT" = 1 ]; then
    ok "$h: rebooting"
    ssh "${SSH_OPTS[@]}" "$h" 'sudo -n systemctl reboot' >/dev/null 2>&1 || true
    sleep 20
    if wait_for_ssh "$h" 300; then
      ok "$h: back up - $(ssh "${SSH_OPTS[@]}" "$h" 'uname -r')"
    else
      bad "$h: DID NOT COME BACK after reboot"; FAILED+=("$h"); continue
    fi
  fi

  ok "$h: done"
done

echo
if [ "${#FAILED[@]}" -eq 0 ]; then
  ok "All ${#HOSTS[@]} host(s) provisioned successfully."
else
  bad "${#FAILED[@]} of ${#HOSTS[@]} host(s) failed: ${FAILED[*]}"
  exit 1
fi
