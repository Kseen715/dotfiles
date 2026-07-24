# lib/pkg.sh — package abstraction with group-by-method dispatch (POSIX sh)
#
# Five verbs cover ~everything (§1): pkg_install, pkg_installed, pkg_refresh,
# pkg_add_repo, pkg_remove. Distro variance lives here + pkgmap/ only.
#
# A logical name resolves through pkgmap/ to a spec whose RHS may carry a
# `method:` tag (§4). No tag = native. pkg_install expands every name, groups by
# method, and dispatches each group — native rows still batch into one call.
#
# MVP providers: native, script, source. cargo/aur/repo/tarball/brew/flatpak are
# recognized tags but error until implemented (see DESIGN "Out of MVP").

# _pkgmap_one <name> — echo the RHS mapped for <name>, or <name> unchanged when
# no row exists (the common case stays zero-effort, §1). Distro map wins over
# the shared any.map.
#
# Facet qualifiers (§1a): a map key may carry an optional @facet, and the most
# specific match wins — codename > version_id > arch > bare name. This is how a
# package's install *method* can differ by distro release (`lsd@jammy`) or CPU
# arch, not just by package manager (G6/G8). A qualified row exists only where
# that facet actually diverges, so the common case is still zero-effort.
_pkgmap_one() {
    _pm_name=$1
    # Candidate keys, most specific first. Facet values are empty on distros
    # that don't report them (${VAR:+...} drops the key entirely then); package
    # names + facet values carry no spaces, so an unquoted expansion is safe.
    for _pm_key in \
        ${OSR_CODENAME:+"$_pm_name@$OSR_CODENAME"} \
        ${OSR_VERSION_ID:+"$_pm_name@$OSR_VERSION_ID"} \
        ${OSR_ARCH:+"$_pm_name@$OSR_ARCH"} \
        "$_pm_name"; do
        for _pm_map in "$OSR_LIB/pkgmap/$OSR_PKG.map" "$OSR_LIB/pkgmap/any.map"; do
            [ -f "$_pm_map" ] || continue
            _pm_line=$(grep "^[[:space:]]*${_pm_key}[[:space:]]*=" "$_pm_map" 2>/dev/null | head -n 1)
            if [ -n "$_pm_line" ]; then
                # strip up to and including the first '='; trim surrounding space
                _pm_rhs=$(printf '%s' "${_pm_line#*=}" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')
                printf '%s' "$_pm_rhs"
                return 0
            fi
        done
    done
    printf '%s' "$_pm_name"
}

# --- native provider ---------------------------------------------------------

# _native_installed <realpkg> — true if the package is present.
_native_installed() {
    case "$OSR_PKG" in
        apt)    dpkg -s "$1" >/dev/null 2>&1 ;;
        dnf)    rpm -q "$1" >/dev/null 2>&1 ;;
        pacman) pacman -Q "$1" >/dev/null 2>&1 ;;
        apk)    apk info -e "$1" >/dev/null 2>&1 ;;
        xbps)   xbps-query "$1" >/dev/null 2>&1 ;;
        *)      return 1 ;;
    esac
}

# _native_held <realpkg> — true if the user has held/pinned it (G2: never
# override user-defined package state).
_native_held() {
    case "$OSR_PKG" in
        apt)    apt-mark showhold 2>/dev/null | grep -qx "$1" ;;
        pacman) grep -E '^[[:space:]]*IgnorePkg' /etc/pacman.conf 2>/dev/null | grep -qw "$1" ;;
        dnf)    grep -rl -E "^[[:space:]]*exclude=.*\b$1\b" /etc/dnf/dnf.conf /etc/yum.repos.d 2>/dev/null | grep -q . ;;
        *)      return 1 ;;
    esac
}

# _via_native <realpkgs...> — filter already-installed and held/pinned, then
# batch-install the rest. Filtering is what makes a second run all-skips (§2).
_via_native() {
    _todo=""
    for _p in "$@"; do
        if _native_installed "$_p"; then
            info "$_p already installed — skipping"
        elif _native_held "$_p"; then
            warn "$_p is held/pinned — skipping"
        else
            _todo="$_todo $_p"
        fi
    done
    [ -n "$_todo" ] || return 0
    # Refresh the package index once per run, lazily — only when we are actually
    # about to install (a fresh container/box has no lists yet).
    if [ -z "${_OSR_REFRESHED:-}" ]; then
        pkg_refresh || warn "package index refresh failed — continuing"
        _OSR_REFRESHED=1
    fi
    # shellcheck disable=SC2086  # intentional word-split into a package list
    case "$OSR_PKG" in
        apt)    as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y $_todo ;;
        dnf)    as_root dnf install -y $_todo ;;
        pacman) as_root pacman -S --needed --noconfirm $_todo ;;
        apk)    as_root apk add $_todo ;;
        xbps)   as_root xbps-install -y $_todo ;;
        *)      error "no native installer for OSR_PKG='$OSR_PKG'" ;;
    esac
    check_error $? "native install failed:$_todo"
}

# --- script provider (curl | sh) ---------------------------------------------
# Spec: script:<url> [args...]  — args are forwarded to the piped installer.
# Idempotency probe: the logical name is the resulting command (§4).
_via_script() {
    _vs_name=$1
    shift
    _vs_url=$1
    shift
    if command -v "$_vs_name" >/dev/null 2>&1; then
        info "$_vs_name already present (script) — skipping"
        return 0
    fi
    info "installing $_vs_name via script installer"
    osr_fetch_stdout "$_vs_url" | as_user sh -s -- "$@"
    check_error $? "script install failed for $_vs_name"
}

# --- source provider (build fn in scope) -------------------------------------
# Spec: source:<builder-fn>  — the builder is a shell function already in scope
# (defined by a lib/build helper or the module). Probe: command exists.
_via_source() {
    _vsrc_name=$1
    _vsrc_fn=$2
    if command -v "$_vsrc_name" >/dev/null 2>&1; then
        info "$_vsrc_name already present (source) — skipping"
        return 0
    fi
    command -v "$_vsrc_fn" >/dev/null 2>&1 \
        || error "source builder '$_vsrc_fn' is not defined for $_vsrc_name"
    info "building $_vsrc_name from source ($_vsrc_fn)"
    "$_vsrc_fn"
    check_error $? "source build failed for $_vsrc_name"
}

# --- verbs -------------------------------------------------------------------

# _spec_method <rhs> — echo the provider method of a resolved spec, or "native".
_spec_method() {
    case "$1" in
        script:*)  echo script ;;
        source:*)  echo source ;;
        cargo:*)   echo cargo ;;
        aur:*)     echo aur ;;
        repo:*)    echo repo ;;
        tarball:*) echo tarball ;;
        brew:*)    echo brew ;;
        flatpak:*) echo flatpak ;;
        *)         echo native ;;
    esac
}

# pkg_install <names...> — expand → group by method → dispatch. Two passes so
# the native batch (which carries downloaders/toolchains like curl) runs BEFORE
# any provider that might need them (§4); native still batches into one call.
pkg_install() {
    # Pass 1: collect and batch-install native packages.
    _native=""
    for _name in "$@"; do
        _rhs=$(_pkgmap_one "$_name")
        [ "$(_spec_method "$_rhs")" = native ] && _native="$_native $_rhs"
    done
    # shellcheck disable=SC2086  # intentional word-split into a package list
    [ -n "$_native" ] && _via_native $_native

    # Pass 2: dispatch provider-tagged specs, in original manifest order.
    for _name in "$@"; do
        _rhs=$(_pkgmap_one "$_name")
        case "$(_spec_method "$_rhs")" in
            native)  ;;  # already handled in pass 1
            script)  _via_script "$_name" ${_rhs#script:} ;;
            source)  _via_source "$_name" "${_rhs#source:}" ;;
            *)       error "provider '${_rhs%%:*}:' not yet implemented ($_name) — MVP covers native/script/source" ;;
        esac
    done
    return 0
}

# pkg_installed <name> — true if <name> is installed under its resolved method.
pkg_installed() {
    _rhs=$(_pkgmap_one "$1")
    case "$_rhs" in
        script:*|source:*|cargo:*|aur:*) command -v "$1" >/dev/null 2>&1 ;;
        *)  for _p in $_rhs; do _native_installed "$_p" || return 1; done ;;
    esac
}

# pkg_refresh — refresh the package index (idempotent).
pkg_refresh() {
    case "$OSR_PKG" in
        apt)    as_root env DEBIAN_FRONTEND=noninteractive apt-get update -q ;;
        dnf)    as_root dnf -q makecache ;;
        pacman) as_root pacman -Sy --noconfirm ;;
        apk)    as_root apk update ;;
        xbps)   as_root xbps-install -S ;;
        *)      error "no refresh verb for OSR_PKG='$OSR_PKG'" ;;
    esac
}

# pkg_remove <names...> — remove native packages (providers own their own).
pkg_remove() {
    _rm=""
    for _name in "$@"; do
        _rhs=$(_pkgmap_one "$_name")
        case "$_rhs" in
            *:*) warn "pkg_remove skips non-native $_name ($_rhs)" ;;
            *)   _rm="$_rm $_rhs" ;;
        esac
    done
    [ -n "$_rm" ] || return 0
    # shellcheck disable=SC2086
    case "$OSR_PKG" in
        apt)    as_root apt-get remove -y $_rm ;;
        dnf)    as_root dnf remove -y $_rm ;;
        pacman) as_root pacman -R --noconfirm $_rm ;;
        apk)    as_root apk del $_rm ;;
        xbps)   as_root xbps-remove -y $_rm ;;
    esac
}

# pkg_add_repo — placeholder for the repo: provider (G1). Not in MVP.
pkg_add_repo() {
    error "pkg_add_repo (repo: provider) is not yet implemented — see DESIGN G1"
}
