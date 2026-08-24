# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/mirrors.sh — rank the distro's package mirrors by speed. POSIX port of
# the legacy standalone .../setup-mirrors.sh, the last un-ported helper of the
# hyprland-glass bundle. It was a script you ran by hand before the installer;
# as a module it is `osr module mirrors`, or a first line in a rice.list.
#
# Deliberately NOT in arch-hyprland-glass/rice.list: ranking probes every mirror
# in the list and takes minutes, which is the wrong thing to do implicitly at the
# top of every install. Opt in when a box actually has slow mirrors.
#
# Rerun-safe (§2) by stamp file, not by re-ranking: the ranking is a snapshot
# that ages, so redoing it is `OSR_MIRRORS_FORCE=1 osr module mirrors` (or
# deleting the stamp), never a silent multi-minute step on a second run.
#
# Only pacman has a first-class ranker in-tree (rankmirrors, from pacman-contrib)
# and only dnf has a built-in fastest-mirror selector. apt/apk/xbps/portage rank
# via out-of-tree tooling or a CDN that already does it, so they log and no-op
# rather than pretending (§9: degrade, never fake it).

OSR_MIRRORS_N=${OSR_MIRRORS_N:-16}          # how many mirrors to keep, ranked
_osr_mirror_stamp=/etc/pacman.d/.osr-mirrors-ranked

# _osr_rank_pacman — rank from the pristine backup, never from an already
# truncated mirrorlist (re-ranking a 16-entry list would just re-confirm whichever
# mirror won the first time). Writes to a temp file and validates it before
# installing, so a failed or empty rankmirrors can never leave the box with no
# mirrors at all - the worst case is the current list, untouched.
#
# Never fatal: a slow/unreachable mirror probe must not kill an install that the
# existing mirrorlist can serve perfectly well. Success is signalled by the stamp
# file, which the caller reads (run_step runs this in a subshell, so a variable
# could not carry the answer back - the filesystem can).
_osr_rank_pacman() {
    _rp_out="${TMPDIR:-/tmp}/osr-mirrorlist.$$"
    rankmirrors -n "$OSR_MIRRORS_N" /etc/pacman.d/mirrorlist.backup >"$_rp_out" 2>/dev/null || :
    if grep -q '^[[:space:]]*Server' "$_rp_out" 2>/dev/null; then
        as_root cp -f "$_rp_out" /etc/pacman.d/mirrorlist
        as_root touch "$_osr_mirror_stamp"
    fi
    rm -f "$_rp_out"
}

case "$OSR_PKG" in
    pacman)
        if [ -f "$_osr_mirror_stamp" ] && [ -z "${OSR_MIRRORS_FORCE:-}" ]; then
            info "mirrors already ranked - skipping (OSR_MIRRORS_FORCE=1 to redo)"
            return 0
        fi

        # No mirrorlist at all (rare, but a broken/emptied /etc/pacman.d is how
        # people get here): fetch the full list and uncomment it, because the
        # published file ships every Server line commented out. This runs BEFORE
        # pkg_install - without mirrors pacman cannot install the ranker either.
        if [ ! -s /etc/pacman.d/mirrorlist ]; then
            warn "no /etc/pacman.d/mirrorlist - fetching the full Arch mirror list"
            _ml_tmp="${TMPDIR:-/tmp}/osr-mirrorlist-all.$$"
            if osr_download "https://archlinux.org/mirrorlist/all/" "$_ml_tmp"; then
                as_root mkdir -p /etc/pacman.d
                sed 's/^[[:space:]]*#[[:space:]]*Server/Server/' "$_ml_tmp" \
                    | as_root tee /etc/pacman.d/mirrorlist >/dev/null
                rm -f "$_ml_tmp"
            else
                rm -f "$_ml_tmp"
                error "could not fetch a mirrorlist and none exists - fix /etc/pacman.d/mirrorlist first"
            fi
        fi

        # One pristine backup, kept forever: it is both the safety net and the
        # input every future ranking reads.
        if [ ! -f /etc/pacman.d/mirrorlist.backup ]; then
            as_root cp -f /etc/pacman.d/mirrorlist /etc/pacman.d/mirrorlist.backup
            info "backed up mirrorlist to /etc/pacman.d/mirrorlist.backup"
        fi

        run_step "Installing mirror-ranking tools" pkg_install pacman-contrib
        run_step "Ranking the $OSR_MIRRORS_N fastest mirrors" _osr_rank_pacman

        if [ -f "$_osr_mirror_stamp" ]; then
            # The index now points at different mirrors - refresh it, and let the
            # rest of the run reuse that refresh (§2).
            pkg_refresh || warn "package index refresh failed - continuing"
            _OSR_REFRESHED=1
        else
            warn "rankmirrors produced no usable list - keeping the existing mirrorlist"
        fi
        ;;
    dnf)
        # dnf ranks on its own; the config just has to ask it to. Appended once
        # (a user-set value is left alone, G2).
        if grep -qE '^[[:space:]]*fastestmirror[[:space:]]*=' /etc/dnf/dnf.conf 2>/dev/null; then
            info "dnf fastestmirror already configured - skipping"
        else
            info "enabling dnf fastestmirror + parallel downloads"
            printf 'fastestmirror=True\nmax_parallel_downloads=10\n' \
                | as_root tee -a /etc/dnf/dnf.conf >/dev/null
        fi
        ;;
    *)
        info "no in-tree mirror ranker for '$OSR_PKG' - skipping"
        ;;
esac
