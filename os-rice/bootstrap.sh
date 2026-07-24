#!/bin/sh
# bootstrap.sh — get a barebone box to the point install.sh can run.
#
# Standalone by necessity: this is the one file that runs BEFORE the repo
# exists, so it cannot source lib/. It re-implements just enough (downloader +
# git presence) to fetch the repo, then hands off to install.sh.
#
#   sh bootstrap.sh --check            report the detected downloader + git, exit
#   sh bootstrap.sh <rice>             clone the repo and install <rice>
#
# Env overrides:
#   OSR_REPO_URL   git URL of the dotfiles repo (default below)
#   OSR_DEST       where to clone (default: ~/.local/share/os-rice-dotfiles)
#
# No compiled binary: if a primitive is truly missing everywhere the answer is
# static busybox, not hand-rolled C (§Decisions).
set -eu

OSR_REPO_URL=${OSR_REPO_URL:-https://github.com/Kseen715/dotfiles.git}
OSR_DEST=${OSR_DEST:-"$HOME/.local/share/os-rice-dotfiles"}

log() { printf '[bootstrap] %s\n' "$*"; }
die() { printf '[bootstrap] error: %s\n' "$*" >&2; exit 1; }

# find_downloader — echo curl | wget | busybox-wget | "" (barebone-first order).
find_downloader() {
    if command -v curl >/dev/null 2>&1; then
        echo curl
    elif command -v wget >/dev/null 2>&1; then
        echo wget
    elif command -v busybox >/dev/null 2>&1 && busybox wget --help >/dev/null 2>&1; then
        echo busybox-wget
    else
        echo ""
    fi
}

# detect_pkg — echo the native package manager, for pulling git if it is missing.
detect_pkg() {
    for _p in apt-get dnf pacman apk xbps-install emerge; do
        if command -v "$_p" >/dev/null 2>&1; then
            case "$_p" in
                apt-get) echo apt ;;
                xbps-install) echo xbps ;;
                emerge) echo portage ;;
                *) echo "$_p" ;;
            esac
            return 0
        fi
    done
    echo ""
}

run_root() { if [ "$(id -u)" -eq 0 ]; then "$@"; else sudo "$@"; fi; }

ensure_git() {
    command -v git >/dev/null 2>&1 && return 0
    log "git not found - installing via $(detect_pkg)"
    case "$(detect_pkg)" in
        apt)    run_root env DEBIAN_FRONTEND=noninteractive apt-get update -q && run_root env DEBIAN_FRONTEND=noninteractive apt-get install -y git ;;
        dnf)    run_root dnf install -y git ;;
        pacman) run_root pacman -Sy --needed --noconfirm git ;;
        apk)    run_root apk add git ;;
        xbps)   run_root xbps-install -y git ;;
        portage) run_root emerge --quiet --noreplace --getbinpkg dev-vcs/git ;;
        *)      die "no package manager found to install git" ;;
    esac
}

# --- --check: report readiness and exit (acceptance harness uses this) --------
if [ "${1:-}" = "--check" ]; then
    _dl=$(find_downloader)
    [ -n "$_dl" ] || die "no downloader found (need curl, wget, or busybox)"
    log "downloader: $_dl"
    log "pkg manager: $(detect_pkg)"
    if command -v git >/dev/null 2>&1; then log "git: present"; else log "git: missing (would install)"; fi
    exit 0
fi

RICE=${1:-}
[ -n "$RICE" ] || die "usage: sh bootstrap.sh <rice>  (or --check)"

[ -n "$(find_downloader)" ] || die "no downloader found (need curl, wget, or busybox)"
ensure_git

if [ -d "$OSR_DEST/.git" ]; then
    log "updating existing clone at $OSR_DEST"
    git -C "$OSR_DEST" pull --ff-only
else
    log "cloning $OSR_REPO_URL -> $OSR_DEST"
    mkdir -p "$(dirname "$OSR_DEST")"
    git clone --depth 1 "$OSR_REPO_URL" "$OSR_DEST"
fi

log "handing off to install.sh $RICE"
exec sh "$OSR_DEST/os-rice/install.sh" "$RICE"
