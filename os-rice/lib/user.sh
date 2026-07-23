# lib/user.sh — target-user model + config-file primitives (POSIX sh)
#
# §8: OSR_USER is the account being riced. root-for-root or user-for-user; most
# work runs as_user and only the native package step escalates to root.

# osr_passwd <user> — echo the user's /etc/passwd line. Uses getent when present
# (handles NSS), falls back to grepping /etc/passwd on busybox/Alpine.
osr_passwd() {
    getent passwd "$1" 2>/dev/null || grep "^$1:" /etc/passwd 2>/dev/null
}

# osr_user_shell <user> — echo the user's login shell (field 7).
osr_user_shell() {
    osr_passwd "$1" | cut -d: -f7
}

# osr_resolve_user [explicit-user] — sets OSR_USER and OSR_HOME.
# Order (§8): --user > $SUDO_USER (when invoked via sudo) > $USER > root.
osr_resolve_user() {
    if [ -n "$1" ]; then
        OSR_USER=$1
    elif [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != "root" ]; then
        OSR_USER=$SUDO_USER
    elif [ -n "${USER:-}" ]; then
        OSR_USER=$USER
    else
        OSR_USER=$(id -un 2>/dev/null || echo root)
    fi

    # Resolve the real home (field 6); handles /root and non-standard homes.
    OSR_HOME=$(osr_passwd "$OSR_USER" | cut -d: -f6)
    if [ -z "$OSR_HOME" ]; then
        [ "$OSR_USER" = "root" ] && OSR_HOME="/root" || OSR_HOME="/home/$OSR_USER"
    fi

    export OSR_USER OSR_HOME
}

# as_user <cmd...> — run as OSR_USER; a no-op wrapper when already that user.
as_user() {
    if [ "$(id -un)" = "$OSR_USER" ]; then
        "$@"
    else
        sudo -u "$OSR_USER" "$@"
    fi
}

# as_root <cmd...> — escalate only the steps that truly need root.
as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        sudo "$@"
    fi
}

# ensure_line <file> <line> — append line if absent (idempotent, §2). Creates
# the file (and parent dir) as OSR_USER when missing.
ensure_line() {
    _el_file=$1
    _el_line=$2
    as_user mkdir -p "$(dirname "$_el_file")"
    if [ -f "$_el_file" ] && grep -qF -- "$_el_line" "$_el_file"; then
        return 0
    fi
    printf '%s\n' "$_el_line" | as_user tee -a "$_el_file" >/dev/null
}

# ensure_block <file> <name> <<'EOF' ... EOF — own a marked region, rewriting
# only between the markers (§5). Reads block body from stdin.
ensure_block() {
    _eb_file=$1
    _eb_name=$2
    _eb_begin="# >>> os-rice:$_eb_name >>>"
    _eb_end="# <<< os-rice:$_eb_name <<<"
    _eb_body=$(cat)
    _eb_tmp=$(mktemp)

    as_user mkdir -p "$(dirname "$_eb_file")"
    if [ -f "$_eb_file" ] && grep -qF "$_eb_begin" "$_eb_file"; then
        # Replace existing region: keep everything outside the markers verbatim.
        awk -v b="$_eb_begin" -v e="$_eb_end" '
            $0 == b { skip = 1; next }
            $0 == e { skip = 0; next }
            !skip   { print }
        ' "$_eb_file" >"$_eb_tmp"
    else
        [ -f "$_eb_file" ] && cat "$_eb_file" >"$_eb_tmp"
    fi

    {
        printf '%s\n' "$_eb_begin"
        printf '%s\n' "$_eb_body"
        printf '%s\n' "$_eb_end"
    } >>"$_eb_tmp"

    as_user cp -f "$_eb_tmp" "$_eb_file"
    rm -f "$_eb_tmp"
}

# backup_copy <src> <dst> — back up dst to dst.bak once, then overwrite as
# OSR_USER (rerun-safe, §2). Skips the copy when contents already match.
backup_copy() {
    _bc_src=$1
    _bc_dst=$2
    [ -f "$_bc_src" ] || error "backup_copy: source not found: $_bc_src"
    # Skip the copy when contents already match — but only if cmp exists (absent
    # on minimal Arch); otherwise just copy (still idempotent, content is equal).
    if [ -f "$_bc_dst" ] && command -v cmp >/dev/null 2>&1 && cmp -s "$_bc_src" "$_bc_dst"; then
        return 0
    fi
    if [ -f "$_bc_dst" ] && [ ! -f "$_bc_dst.bak" ]; then
        as_user cp -f "$_bc_dst" "$_bc_dst.bak"
    fi
    as_user mkdir -p "$(dirname "$_bc_dst")"
    as_user cp -f "$_bc_src" "$_bc_dst"
}
