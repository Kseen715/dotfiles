# lib/net.sh — downloading + version resolution (POSIX sh)
#
# One place for "fetch a URL" and "what's the latest GitHub tag" so providers
# (script:, tarball:, source:) don't each re-hand-roll curl/wget (G4).

# osr_downloader — echo the first available downloader backend, or "" if none.
# Order: curl, wget, busybox wget (the barebone-Alpine fallback).
osr_downloader() {
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

# osr_ensure_downloader — guarantee a downloader exists, installing curl via the
# native package manager when none is present. Makes download-backed providers
# (tarball/script/deb) work on a minimal box even when invoked standalone (e.g.
# `osr module gh` on a fresh image where the rice's curl was never installed).
osr_ensure_downloader() {
    [ -n "$(osr_downloader)" ] && return 0
    # This is a side-effect install; its output must go to stderr so it never
    # pollutes a fetch stream (osr_fetch_stdout's stdout IS the payload, often
    # piped straight into `sh`/`bash`).
    if command -v pkg_install >/dev/null 2>&1; then
        pkg_install curl >&2
    fi
    [ -n "$(osr_downloader)" ] || error "no downloader found (need curl, wget, or busybox)"
}

# _osr_download_run <url> <dest> — the bare fetch, one backend, no progress.
_osr_download_run() {
    case "$(osr_downloader)" in
        curl)        curl -fsSL -o "$2" "$1" ;;
        wget)        wget -qO "$2" "$1" ;;
        busybox-wget) busybox wget -qO "$2" "$1" ;;
        *)           error "no downloader found (need curl, wget, or busybox)" ;;
    esac
}

# _osr_file_size <path> — bytes in a file, 0 when it does not exist yet. GNU/
# busybox stat first, BSD stat second; `wc -c` is the last resort only because it
# reads the whole file, which on the payloads this polls is the thing to avoid.
_osr_file_size() {
    stat -c %s "$1" 2>/dev/null && return 0
    stat -f %z "$1" 2>/dev/null && return 0
    wc -c <"$1" 2>/dev/null | tr -d ' ' || echo 0
}

# _osr_fit_left <text> <width> — echo text truncated to width, tail-cropped with
# a "..." marker so the byte counts that follow it on the line are never the part
# that falls off the right edge (§3: ASCII, no unicode ellipsis). A width too
# small for even the marker yields nothing, which is the honest answer on a very
# narrow terminal - the numbers are what matter.
_osr_fit_left() {
    _fl_text=$1
    _fl_w=$2
    [ "$_fl_w" -lt 4 ] && { printf ''; return 0; }
    if [ "${#_fl_text}" -le "$_fl_w" ]; then
        printf '%s' "$_fl_text"
    else
        printf '%s...' "$(printf '%s' "$_fl_text" | cut -c "1-$((_fl_w - 3))")"
    fi
}

# OSR_PROGRESS_MIN_BYTES — below this, a download prints nothing. A meter for a
# 30 KB tarball is noise; the number is roughly "big enough that the terminal
# looks hung without one".
OSR_PROGRESS_MIN_BYTES=${OSR_PROGRESS_MIN_BYTES:-1048576}   # 1 MiB

# _osr_head <url> — echo the response headers of a HEAD, one block per redirect
# hop, CRs stripped. Empty when the downloader has no header-only mode (busybox
# wget) or the host rejects a HEAD.
_osr_head() {
    case "$(osr_downloader)" in
        curl) curl -fsSLI --max-time 20 "$1" 2>/dev/null ;;
        wget) wget --spider -S --timeout=20 -q -O /dev/null "$1" 2>&1 ;;
        *)    : ;;
    esac | tr -d '\r'
}

# _osr_remote_size <url> — echo the Content-Length a HEAD reports, or "" when the
# server does not say (chunked responses, a HEAD the host rejects, busybox wget,
# which has no header-only mode). Redirects are followed, and the LAST
# Content-Length wins: a redirect chain prints one header block per hop, and only
# the final hop describes the payload.
#
# Sizes are advisory here - they only scale a progress bar - so every failure
# path is an empty answer and a silent download, never an error.
_osr_remote_size() {
    _osr_head "$1" | sed -n 's/^[Cc]ontent-[Ll]ength:[[:space:]]*\([0-9][0-9]*\).*/\1/p' \
         | tail -n 1
}

# osr_final_url <url> — echo where a redirecting URL actually lands, or "" when
# nothing redirects (or no HEAD is possible). The LAST Location wins, same reason
# as the size above. This is how a vendor "latest" URL yields a version: the
# filename it redirects to carries one (telegram.org/dl/desktop/linux ->
# .../tsetup.7.0.9.tar.xz), so nothing has to be hard-coded (G4).
osr_final_url() {
    _osr_head "$1" | sed -n 's/^[Ll]ocation:[[:space:]]*\([^[:space:]]*\).*/\1/p' | tail -n 1
}

# osr_download <url> <dest> [expected_bytes] — fetch url to dest with whatever
# downloader exists, printing progress for anything big.
#
# The size comes from the caller when it already knows one (a release feed that
# publishes byte counts), and otherwise from a HEAD on the URL - one small round
# trip, so most downloads get a meter without every builder having to learn its
# upstream's metadata. No size, or one under OSR_PROGRESS_MIN_BYTES, means the
# plain silent fetch.
#
# Progress is printed as ordinary NEWLINE-terminated lines, polled off the
# growing file rather than taken from the downloader. Both halves are deliberate:
# no backend agrees on a progress format (curl -#, wget --progress=bar,
# busybox's dots), and all of them redraw with \r on one line - which the
# run_step live window, being a `tail` over a logfile, renders as one endless
# line. A line every few seconds is what that window can actually show, and it
# degrades to plain scrollback when piped.
osr_download() {
    _dl_url=$1
    _dl_dest=$2
    _dl_total=${3:-}
    osr_ensure_downloader
    [ -n "$_dl_total" ] || _dl_total=$(_osr_remote_size "$_dl_url")
    case "$_dl_total" in ''|*[!0-9]*) _dl_total=0 ;; esac
    if [ "$_dl_total" -lt "$OSR_PROGRESS_MIN_BYTES" ]; then
        _osr_download_run "$_dl_url" "$_dl_dest"
        return $?
    fi

    # Each line names what is being fetched: several steps download something,
    # and a bare percentage in the live window says nothing about which.
    _dl_name=${_dl_url##*/}
    _dl_name=${_dl_name%%\?*}
    _osr_download_run "$_dl_url" "$_dl_dest" &
    _dl_pid=$!
    _dl_last=-1
    while kill -0 "$_dl_pid" 2>/dev/null; do
        sleep "${OSR_DOWNLOAD_POLL:-3}"
        _dl_now=$(_osr_file_size "$_dl_dest")
        [ -n "$_dl_now" ] || _dl_now=0
        _dl_pct=$((_dl_now * 100 / _dl_total))
        [ "$_dl_pct" -gt 100 ] && _dl_pct=100
        if [ "$_dl_pct" -ne "$_dl_last" ]; then
            _dl_num=$(printf '%s MiB / %s MiB (%s%%)' \
                "$((_dl_now / 1048576))" "$((_dl_total / 1048576))" "$_dl_pct")
            printf '  %s %s\n' "$(_osr_fit_left "$_dl_name" "$((${OSR_COLS:-80} - ${#_dl_num} - 3))")" "$_dl_num"
            _dl_last=$_dl_pct
        fi
    done
    wait "$_dl_pid"
}

# osr_fetch_stdout <url> — stream a URL to stdout (for `curl | sh` installers).
osr_fetch_stdout() {
    _fs_url=$1
    osr_ensure_downloader
    case "$(osr_downloader)" in
        curl)        curl -fsSL "$_fs_url" ;;
        wget)        wget -qO- "$_fs_url" ;;
        busybox-wget) busybox wget -qO- "$_fs_url" ;;
        *)           error "no downloader found (need curl, wget, or busybox)" ;;
    esac
}

# github_latest <owner/repo> — echo the latest release tag (e.g. v1.2.3).
# Uses the releases/latest API; falls back to the first tag if that 404s.
github_latest() {
    _gl_repo=$1
    _gl_json=$(osr_fetch_stdout "https://api.github.com/repos/$_gl_repo/releases/latest" 2>/dev/null)
    _gl_tag=$(printf '%s\n' "$_gl_json" | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)
    if [ -z "$_gl_tag" ]; then
        _gl_json=$(osr_fetch_stdout "https://api.github.com/repos/$_gl_repo/tags" 2>/dev/null)
        _gl_tag=$(printf '%s\n' "$_gl_json" | sed -n 's/.*"name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)
    fi
    [ -n "$_gl_tag" ] || error "github_latest: could not resolve a tag for $_gl_repo"
    printf '%s' "$_gl_tag"
}
