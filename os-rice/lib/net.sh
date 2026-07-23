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

# osr_download <url> <dest> — fetch url to dest with whatever downloader exists.
osr_download() {
    _dl_url=$1
    _dl_dest=$2
    case "$(osr_downloader)" in
        curl)        curl -fsSL -o "$_dl_dest" "$_dl_url" ;;
        wget)        wget -qO "$_dl_dest" "$_dl_url" ;;
        busybox-wget) busybox wget -qO "$_dl_dest" "$_dl_url" ;;
        *)           error "no downloader found (need curl, wget, or busybox)" ;;
    esac
}

# osr_fetch_stdout <url> — stream a URL to stdout (for `curl | sh` installers).
osr_fetch_stdout() {
    _fs_url=$1
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
