# lib/user.sh — the shell-callable surface of the user model (POSIX sh)
#
# §8: OSR_USER is the account being riced. root-for-root or user-for-user; most
# work runs as_user and only the native package step escalates to root.
#
# `osr user` in the harness core (lib/user.c) makes every decision here —
# who the user is, where they live, whether a shell is already theirs, what the
# rewritten file should contain. What stays is the writing, because it goes
# through as_user/as_root: those are shell functions wrapping `sudo -u`/`sudo`,
# modules use them as command prefixes (`as_root pacman -S ...`), and a write
# done in the core would land as the wrong owner.
#
# Byte-for-byte the sh original, frozen at test/ref/user_sh_ref.sh and diffed
# by test/unit/user_c_parity.sh.

if [ -z "${OSR_BIN:-}" ]; then
    . "${OSR_LIB:?user.sh: source lib/ui.sh first, or export OSR_LIB}/ui.sh"
fi

# osr_passwd <user> — echo the user's /etc/passwd line (NSS first, file second).
osr_passwd() { "$OSR_BIN" user passwd "$1"; }

# osr_user_shell <user> — echo the user's login shell (field 7).
osr_user_shell() { "$OSR_BIN" user shell "$1"; }

# osr_realpath <path> — canonical path when one can be had, else the input
# unchanged. Used to compare shells across the /bin -> /usr/bin symlink split
# (passwd says /bin/zsh, `command -v` says /usr/bin/zsh: same binary).
osr_realpath() { "$OSR_BIN" user realpath "$1"; }

# osr_shell_is <user> <shell> — true when <user> already logs in with <shell>,
# comparing canonical paths so an aliased path doesn't cause a pointless reset.
osr_shell_is() { "$OSR_BIN" user shell-is "$1" "$2"; }

# The two system files this writes. They are literals, with an override a test
# can set to sandbox them — the core honours the same two names. Deliberately
# NOT defaulted into the environment: OSR_PASSWD_FILE being set at all tells
# the core to stop consulting NSS, which is only ever right in a sandbox.

# osr_register_shell <shell> — make sure <shell> is listed in /etc/shells
# (idempotent, §2). Not every distro's zsh package registers itself, and an
# unlisted shell makes chsh refuse for non-root and makes some login managers
# and terminals treat the account as having no valid shell.
osr_register_shell() {
    [ -n "$1" ] || return 0
    "$OSR_BIN" user shell-registered "$1" && return 0
    printf '%s\n' "$1" | as_root tee -a "${OSR_SHELLS_FILE:-/etc/shells}" >/dev/null
}

# osr_passwd_set_shell <user> <shell> — last-resort login shell change: rewrite
# field 7 in /etc/passwd. For busybox boxes that ship neither chsh nor usermod.
# Skips users that don't live in /etc/passwd (NSS/LDAP), and writes with cp so
# the existing inode keeps its mode, owner and SELinux context.
osr_passwd_set_shell() {
    _pss_tmp=$(mktemp)
    if ! "$OSR_BIN" user passwd-shell "$1" "$2" >"$_pss_tmp" 2>/dev/null; then
        rm -f "$_pss_tmp"
        return 1
    fi
    [ -s "$_pss_tmp" ] || { rm -f "$_pss_tmp"; return 1; }
    as_root cp -f "$_pss_tmp" "${OSR_PASSWD_FILE:-/etc/passwd}"
    rm -f "$_pss_tmp"
}

# osr_set_login_shell <user> <shell> — set the login shell for real, whatever
# the box provides. `chsh` is NOT universal: busybox (Alpine) has no applet for
# it and a minimal Fedora leaves it in the optional util-linux-user package, so
# a chsh-only implementation silently leaves those systems on /bin/sh. Try each
# mechanism in turn and verify the result rather than trusting an exit code:
#   chsh (util-linux/shadow) -> usermod (shadow) -> direct /etc/passwd rewrite.
# Returns non-zero when the shell still isn't <shell> afterwards.
osr_set_login_shell() {
    _sls_user=$1
    _sls_shell=$2

    osr_register_shell "$_sls_shell" || true

    if command -v chsh >/dev/null 2>&1; then
        as_root chsh -s "$_sls_shell" "$_sls_user" || true
    fi
    if ! osr_shell_is "$_sls_user" "$_sls_shell" && command -v usermod >/dev/null 2>&1; then
        as_root usermod -s "$_sls_shell" "$_sls_user" || true
    fi
    if ! osr_shell_is "$_sls_user" "$_sls_shell"; then
        osr_passwd_set_shell "$_sls_user" "$_sls_shell" || true
    fi

    osr_shell_is "$_sls_user" "$_sls_shell"
}

# osr_resolve_user [explicit-user] — sets OSR_USER and OSR_HOME.
# Order (§8): --user > $SUDO_USER (when invoked via sudo) > $USER > root.
osr_resolve_user() {
    eval "$("$OSR_BIN" user resolve "${1:-}")"
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
    "$OSR_BIN" user needs-line "$_el_file" "$_el_line" || return 0
    printf '%s\n' "$_el_line" | as_user tee -a "$_el_file" >/dev/null
}

# ensure_block <file> <name> <<'EOF' ... EOF — own a marked region, rewriting
# only between the markers (§5). Reads block body from stdin.
ensure_block() {
    _eb_tmp=$(mktemp)
    as_user mkdir -p "$(dirname "$1")"
    "$OSR_BIN" user compose-block "$1" "$2" >"$_eb_tmp"
    as_user cp -f "$_eb_tmp" "$1"
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
    if [ -f "$_bc_dst" ] && command -v cmp >/dev/null 2>&1 &&
       "$OSR_BIN" user same-content "$_bc_src" "$_bc_dst"; then
        return 0
    fi
    if [ -f "$_bc_dst" ] && [ ! -f "$_bc_dst.bak" ]; then
        as_user cp -f "$_bc_dst" "$_bc_dst.bak"
    fi
    as_user mkdir -p "$(dirname "$_bc_dst")"
    as_user cp -f "$_bc_src" "$_bc_dst"
}
