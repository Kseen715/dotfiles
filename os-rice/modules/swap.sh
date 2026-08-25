# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/swap.sh — memory: zram first, disk swap only for the rest. ONE copy,
# POSIX (was .../linux-arch-x86_64-hyprland-glass/setup-swap.sh, which hard-coded
# a 24G /swapfile regardless of RAM, free disk, or the swap already present).
#
# Sizing, all MiB. Inputs are files/commands so the whole plan is mockable:
# OSR_MEMINFO, OSR_PROC_SWAPS, OSR_SWAPFILE, OSR_FSTAB, OSR_ZRAM_CONF,
# OSR_SYSCTL_CONF. The four knobs below are the only policy dials.
#
#   zram   RAM <  24G -> RAM <= 8G: cover RAM in full (it compresses ~2-3x, so
#                        this costs far less than its nominal size and keeps a
#                        small box off the disk entirely)
#                        RAM >  8G: min(RAM/2, 8G) - past that the CPU cost of
#                        compressing outweighs the pages saved
#          RAM >= 24G -> NONE. There is nothing to rescue: the box is not going
#                        to thrash, and zram's swap device would compete with
#                        the disk swap that suspend-to-disk resumes from.
#   disk   RAM <= 16G  -> RAM: sized so hibernation/hybrid-sleep can write the
#                        whole image (needs `resume=` on the kernel cmdline too;
#                        this module sizes the space, it does not edit the
#                        bootloader)
#          RAM >  16G  -> 16G: hibernating 24G+ means writing 24G+ on every
#                        sleep - not worth the disk or the wait, so this is
#                        plain overflow swap and hibernation is off the table
#   priority    zram 100, disk swapfile 10 - both live at once, and the kernel
#                        fills the higher priority first: pages go to RAM-speed
#                        compressed swap and only spill to the disk when zram is
#                        full. (A swap PARTITION keeps whatever priority its own
#                        fstab line gives it; that line is the user's, not ours.)
#   swappiness  zram present -> 100 + page-cluster=0 (compressed swap is cheap
#                        and single-page, so lean on it - the disk stays out of
#                        the way by priority, not by a timid swappiness)
#               no zram      -> 10 (disk swap under a game/compile is stutter;
#                        keep it for emergencies, not for routine reclaim)
#
# An existing swap PARTITION counts in full toward the disk target; only the
# remainder becomes a swapfile on the root fs (the system drive, normally the
# fastest one). That file is capped at half the free space there and rounded up
# to whole GiB; a deficit under 1G buys nothing, so no file is made.

# A container/WSL guest does not own the memory it runs on - the host does.
case "${OSR_VIRT:-none}" in
    docker|podman|lxc|lxc-libvirt|systemd-nspawn|wsl|openvz)
        info "swap: ${OSR_VIRT} guest - the host owns swap/zram, skipping"
        return 0 ;;
esac

OSR_SWAPFILE=${OSR_SWAPFILE:-/swapfile}
_swap_meminfo=${OSR_MEMINFO:-/proc/meminfo}
_swap_swaps=${OSR_PROC_SWAPS:-/proc/swaps}
_swap_fstab=${OSR_FSTAB:-/etc/fstab}
_swap_zconf=${OSR_ZRAM_CONF:-/etc/systemd/zram-generator.conf}
_swap_zramen_conf_path=${OSR_ZRAMEN_CONF:-/etc/sv/zramen/conf}
_swap_sysctl=${OSR_SYSCTL_CONF:-/etc/sysctl.d/99-osr-swap.conf}

# Policy knobs (MiB). Override in the environment to retune without editing.
_swap_zram_off_at=${OSR_ZRAM_OFF_AT:-24576}    # RAM at/above which zram is off
_swap_hib_max=${OSR_HIBERNATE_MAX_RAM:-16384}  # largest RAM still sized to hibernate
_swap_disk_cap=${OSR_SWAP_MAX:-16384}          # overflow-only swap above that
_swap_file_prio=${OSR_SWAPFILE_PRIO:-10}       # below zram's 100: zram fills first
_swap_reserve=${OSR_SWAP_RESERVE:-2048}        # free space the swapfile must leave behind

# _swap_gib_up <mib> — round up to a whole GiB.
_swap_gib_up() { echo $(( ($1 + 1023) / 1024 * 1024 )); }

# swap_plan — read the machine, set SWAP_RAM / SWAP_RAM_TIER / SWAP_ZRAM_WANT / SWAP_DISK_WANT /
# SWAP_HAVE_{ZRAM,PART,FILE} / SWAP_FREE / SWAP_FILE_WANT. Pure measurement +
# arithmetic: it mutates nothing, so the unit test can call it per fixture.
swap_plan() {
    [ -r "$_swap_meminfo" ] || error "swap: cannot read $_swap_meminfo"
    SWAP_RAM=$(awk '/^MemTotal:/ { print int($2 / 1024); exit }' "$_swap_meminfo")
    [ -n "$SWAP_RAM" ] && [ "$SWAP_RAM" -gt 0 ] || error "swap: no MemTotal in $_swap_meminfo"

    # Tier on the INSTALLED size, not MemTotal: the kernel/firmware reserve a
    # few hundred MiB, so a 24G machine reports ~23.4G and would fall a tier
    # short. RAM ships in 2GiB steps, so rounding up to the next 2GiB recovers
    # the number on the spec sheet without ever inventing a tier.
    SWAP_RAM_TIER=$(( (SWAP_RAM + 2047) / 2048 * 2048 ))

    if [ "$SWAP_RAM_TIER" -ge "$_swap_zram_off_at" ]; then
        SWAP_ZRAM_WANT=0
    elif [ "$SWAP_RAM_TIER" -le 8192 ]; then
        SWAP_ZRAM_WANT=$SWAP_RAM_TIER
    elif [ $((SWAP_RAM_TIER / 2)) -gt 8192 ]; then
        SWAP_ZRAM_WANT=8192
    else
        SWAP_ZRAM_WANT=$((SWAP_RAM_TIER / 2))
    fi

    if [ "$SWAP_RAM_TIER" -le "$_swap_hib_max" ]; then
        SWAP_DISK_WANT=$SWAP_RAM_TIER          # hibernation image fits
    else
        SWAP_DISK_WANT=$_swap_disk_cap         # overflow only, no hibernation
    fi

    # Whether zram will actually EXIST, which is not the same as wanting it.
    # zram is a kernel module and is init-agnostic; what is systemd-bound is
    # zram-generator, which IS a systemd generator. So each init gets its own
    # setter and only an init with neither falls back to no zram:
    #   systemd -> zram-generator      runit -> zramen (Void's, /etc/sv/zramen)
    # swappiness then follows what the machine ends up with rather than what it
    # asked for - deriving it from the want is how a box whose init cannot
    # provide zram still gets swappiness=100 and page-cluster=0 against a lone
    # disk swapfile, which is the exact opposite of the intended policy.
    case "${OSR_INIT:-}" in
        systemd|runit) _sp_zram_reachable=1 ;;
        *)             _sp_zram_reachable=0 ;;
    esac
    if [ "$SWAP_ZRAM_WANT" -gt 0 ] && [ "$_sp_zram_reachable" -eq 1 ]; then
        SWAP_ZRAM_ACTIVE=1
    else
        SWAP_ZRAM_ACTIVE=0
    fi

    # zramen takes a PERCENTAGE of RAM plus an absolute MB ceiling, so the same
    # policy has to be expressed both ways. The percentage carries the shape
    # (cover RAM, or half of it) and ZRAM_MAX_SIZE pins the 8G ceiling exactly,
    # because a percentage of the real MemTotal would drift off the tier.
    if [ "$SWAP_RAM_TIER" -gt 0 ]; then
        SWAP_ZRAM_PCT=$(( SWAP_ZRAM_WANT * 100 / SWAP_RAM_TIER ))
    else
        SWAP_ZRAM_PCT=0
    fi
    [ "$SWAP_ZRAM_PCT" -le 100 ] || SWAP_ZRAM_PCT=100

    # zram is cheap and page-at-a-time; disk swap under load is stutter.
    if [ "$SWAP_ZRAM_ACTIVE" -eq 1 ]; then SWAP_SWAPPINESS=100; else SWAP_SWAPPINESS=10; fi

    # What is active now. /proc/swaps sizes are KiB; zram shows up as a
    # "partition" named /dev/zramN, so match the name before the type.
    SWAP_HAVE_ZRAM=0; SWAP_HAVE_PART=0; SWAP_HAVE_FILE=0
    if [ -r "$_swap_swaps" ]; then
        read -r SWAP_HAVE_ZRAM SWAP_HAVE_PART SWAP_HAVE_FILE <<EOF
$(awk 'NR > 1 { m = int($3 / 1024)
                if ($1 ~ /^\/dev\/zram/)  z += m
                else if ($2 == "partition") p += m
                else                        f += m }
       END { printf "%d %d %d\n", z + 0, p + 0, f + 0 }' "$_swap_swaps")
EOF
    fi

    SWAP_FREE=$(df -Pk "$(dirname "$OSR_SWAPFILE")" 2>/dev/null | awk 'NR == 2 { print int($4 / 1024) }')
    [ -n "$SWAP_FREE" ] || SWAP_FREE=0

    _sp_need=$((SWAP_DISK_WANT - SWAP_HAVE_PART))
    [ "$_sp_need" -gt 0 ] || _sp_need=0
    _sp_need=$(_swap_gib_up "$_sp_need")
    # The current swapfile is deleted before a new one is written, so its blocks
    # are free space too. Two limits, whichever bites first: never take more than
    # half of what is available (a ratio, for big disks), and always leave
    # _swap_reserve behind (an absolute floor - half of a nearly-full disk is
    # still not enough room to survive an update).
    _sp_avail=$(( SWAP_FREE + SWAP_HAVE_FILE ))
    _sp_cap=$(( _sp_avail / 2 ))
    _sp_room=$(( _sp_avail - _swap_reserve ))
    [ "$_sp_cap" -le "$_sp_room" ] || _sp_cap=$_sp_room
    [ "$_sp_cap" -ge 0 ] || _sp_cap=0
    if [ "$_sp_need" -gt "$_sp_cap" ]; then
        warn "swap: want ${_sp_need}M of swapfile but only ${_sp_cap}M is spendable on $(dirname "$OSR_SWAPFILE") - capping"
        _sp_need=$(( _sp_cap / 1024 * 1024 ))
    fi
    [ "$_sp_need" -ge 1024 ] || _sp_need=0
    SWAP_FILE_WANT=$_sp_need
}

# _swap_zram_conf — the zram-generator drop-in for the computed size. A literal
# size, not an expression, so the plan and the config can never disagree.
# ponytail: after a RAM change, re-run this module to resize.
_swap_zram_conf() {
    printf '# managed by os-rice (modules/swap.sh)\n[zram0]\nzram-size = %s\ncompression-algorithm = zstd\nswap-priority = 100\n' \
        "$SWAP_ZRAM_WANT"
}

# _swap_zramen_conf — Void's zramen reads /etc/sv/zramen/conf as plain shell.
# Keys are the ones the packaged conf documents (ZRAM_SIZE is a percentage,
# ZRAM_MAX_SIZE an MB cap); zstd and priority 100 keep it in step with the
# zram-generator drop-in so both inits land on the same policy.
_swap_zramen_conf() {
    printf '# managed by os-rice (modules/swap.sh)\n'
    printf 'export ZRAM_COMP_ALGORITHM=zstd\n'
    printf 'export ZRAM_PRIORITY=100\n'
    printf 'export ZRAM_SIZE=%s\n' "$SWAP_ZRAM_PCT"
    printf 'export ZRAM_MAX_SIZE=%s\n' "$SWAP_ZRAM_WANT"
}

_swap_apply_zramen() {
    _swap_zramen_conf | as_root tee "$_swap_zramen_conf_path" >/dev/null
    enable_service zramen
    # The service only reads conf at start, so a resize needs a restart. `sv` is
    # a no-op-with-a-message when the service was only just linked.
    as_root sv restart zramen >/dev/null 2>&1 || true
}

_swap_apply_zram() {
    _swap_zram_conf | as_root tee "$_swap_zconf" >/dev/null
    as_root systemctl daemon-reload
    as_root systemctl restart systemd-zram-setup@zram0.service
}

# _swap_disable_zram — tear down zram we configured earlier (a RAM upgrade past
# the threshold, or a retuned knob). Only ever removes OUR drop-in.
_swap_disable_zram() {
    as_root rm -f "$_swap_zconf"
    as_root systemctl daemon-reload
    as_root swapoff /dev/zram0
}

# --- sysctl: how hard the kernel leans on whatever swap it ended up with -----
_swap_sysctl_conf() {
    printf '# managed by os-rice (modules/swap.sh)\nvm.swappiness = %s\n' "$SWAP_SWAPPINESS"
    # zram is single-page and cheap: reading ahead 8 pages per fault only wastes
    # decompression. Irrelevant (and unset) when swap is a disk - including the
    # case where zram was WANTED but the init could not provide it.
    [ "$SWAP_ZRAM_ACTIVE" -eq 1 ] && printf 'vm.page-cluster = 0\n'
    return 0
}

_swap_apply_sysctl() {
    _swap_sysctl_conf | as_root tee "$_swap_sysctl" >/dev/null
    as_root sysctl -p "$_swap_sysctl"
}

# _swap_make_file — (re)create the swapfile at SWAP_FILE_WANT MiB and enable it.
# chattr +C on the empty file is the btrfs requirement (no CoW, no compression);
# it is a harmless no-op on ext4/xfs. mkswap --file allocates the file itself on
# util-linux >= 2.38; older ones need the dd fallback.
_swap_make_file() {
    as_root sh -c "
        set -e

        # A lock, because the fast path below was once the slow path: when this
        # step appears to hang, the natural thing to do is run the module again,
        # and two concurrent 4 GiB writes to the same file corrupt both.
        exec 9>'$OSR_SWAPFILE.lock'
        if command -v flock >/dev/null 2>&1 && ! flock -n 9; then
            echo 'swap: another run is already building $OSR_SWAPFILE - skipping' >&2
            exit 0
        fi

        swapoff '$OSR_SWAPFILE' 2>/dev/null || true
        rm -f '$OSR_SWAPFILE'

        # mkswap --file creates the file ITSELF, and that is the whole trick:
        # it fallocates (instant, 0.04s for 4 GiB) and applies nocow on btrfs,
        # both of which this module used to do by hand. Pre-creating the file
        # with touch makes it fail - 'cannot set permissions on swap file:
        # Success' - because it expects to own the creation. That failure used
        # to be swallowed by 2>/dev/null and silently fell through to dd, which
        # writes 4 GiB one megabyte at a time behind a spinner with no progress:
        # indistinguishable from a hang, and the reason for the lock above.
        #
        # stderr is NOT redirected any more. If this fails the message is the
        # only thing that explains the fallback, and it costs nothing to keep.
        if mkswap -U clear --size ${SWAP_FILE_WANT}M --file '$OSR_SWAPFILE'; then
            :
        else
            # util-linux older than 2.38 has no --file. Allocate by hand, and
            # prefer fallocate over dd for the same reason mkswap does.
            rm -f '$OSR_SWAPFILE'
            touch '$OSR_SWAPFILE'
            chattr +C '$OSR_SWAPFILE' 2>/dev/null || true
            chmod 600 '$OSR_SWAPFILE'
            fallocate -l ${SWAP_FILE_WANT}M '$OSR_SWAPFILE' 2>/dev/null ||
                dd if=/dev/zero of='$OSR_SWAPFILE' bs=1M count=$SWAP_FILE_WANT
            mkswap '$OSR_SWAPFILE'
        fi

        chmod 600 '$OSR_SWAPFILE'
        swapon --priority $_swap_file_prio '$OSR_SWAPFILE'
        rm -f '$OSR_SWAPFILE.lock'
    "
}

# ensure_line writes as OSR_USER; /etc/fstab needs root, so append it here.
_swap_add_fstab() {
    printf '%s none swap defaults,pri=%s 0 0\n' "$OSR_SWAPFILE" "$_swap_file_prio" |
        as_root tee -a "$_swap_fstab" >/dev/null
}

swap_plan
info "swap: ram=${SWAP_RAM}M | zram want=${SWAP_ZRAM_WANT}M have=${SWAP_HAVE_ZRAM}M | disk want=${SWAP_DISK_WANT}M have partition=${SWAP_HAVE_PART}M file=${SWAP_HAVE_FILE}M free=${SWAP_FREE}M -> swapfile ${SWAP_FILE_WANT}M"

# --- zram (systemd-only: zram-generator is a systemd generator) ---------------
if [ "$SWAP_ZRAM_WANT" -eq 0 ]; then
    info "swap: ${SWAP_RAM}M RAM is at/above the ${_swap_zram_off_at}M zram threshold - no zram (keeps suspend-to-disk clean)"
    if [ "$SWAP_HAVE_ZRAM" -gt 0 ] || [ -f "$_swap_zconf" ]; then
        run_step "Disabling zram" _swap_disable_zram
    fi
elif [ "${OSR_INIT:-}" = systemd ]; then
    run_step "Installing zram-generator" pkg_install zram-generator
    if [ "$SWAP_HAVE_ZRAM" -gt 0 ] && [ "$(cat "$_swap_zconf" 2>/dev/null)" = "$(_swap_zram_conf)" ]; then
        info "zram already active at ${SWAP_ZRAM_WANT}M, skipping"
    else
        run_step "Configuring zram (${SWAP_ZRAM_WANT}M)" _swap_apply_zram
    fi
elif [ "${OSR_INIT:-}" = runit ]; then
    run_step "Installing zramen" pkg_install zramen
    if [ "$SWAP_HAVE_ZRAM" -gt 0 ] && [ "$(cat "$_swap_zramen_conf_path" 2>/dev/null)" = "$(_swap_zramen_conf)" ]; then
        info "zram already active at ${SWAP_ZRAM_WANT}M, skipping"
    else
        run_step "Configuring zram via zramen (${SWAP_ZRAM_WANT}M, ${SWAP_ZRAM_PCT}% of RAM)" \
            _swap_apply_zramen
    fi
else
    warn "swap: no zram setter for init=${OSR_INIT:-unknown} (systemd uses zram-generator, runit uses zramen) - skipping zram"
fi

# --- disk swap ---------------------------------------------------------------
if [ "$SWAP_FILE_WANT" -eq 0 ]; then
    info "swap: ${SWAP_HAVE_PART}M of swap partition covers the ${SWAP_DISK_WANT}M target, no swapfile needed"
elif [ "$SWAP_HAVE_FILE" -eq "$SWAP_FILE_WANT" ]; then
    info "swap: $OSR_SWAPFILE already ${SWAP_FILE_WANT}M, skipping"
else
    run_step "Creating $OSR_SWAPFILE (${SWAP_FILE_WANT}M)" _swap_make_file
    if grep -q "^[[:space:]]*$OSR_SWAPFILE[[:space:]]" "$_swap_fstab" 2>/dev/null; then
        info "swap: $OSR_SWAPFILE already in $_swap_fstab"
    else
        run_step "Adding $OSR_SWAPFILE to $_swap_fstab" _swap_add_fstab
    fi
fi

# --- swappiness --------------------------------------------------------------
if [ "$(cat "$_swap_sysctl" 2>/dev/null)" = "$(_swap_sysctl_conf)" ]; then
    info "swap: vm.swappiness already $SWAP_SWAPPINESS, skipping"
else
    run_step "Setting vm.swappiness=$SWAP_SWAPPINESS" _swap_apply_sysctl
fi

# Hibernation is a property of the DISK swap only - zram cannot hold the image.
_swap_disk_total=$((SWAP_HAVE_PART + SWAP_FILE_WANT))
[ "$SWAP_FILE_WANT" -gt 0 ] || _swap_disk_total=$((SWAP_HAVE_PART + SWAP_HAVE_FILE))
if [ "$_swap_disk_total" -ge "$SWAP_RAM" ]; then
    info "swap: ${_swap_disk_total}M of disk swap >= RAM - hibernation fits (add resume=<swap dev> [+ resume_offset= for a swapfile] to the kernel cmdline to actually use it)"
else
    info "swap: ${_swap_disk_total}M of disk swap < ${SWAP_RAM}M RAM - suspend-to-RAM only, no hibernation"
fi
