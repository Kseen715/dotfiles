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

# --- facet version arithmetic (§1a) ------------------------------------------
# Distro releases are numbered, and the interesting question about one is almost
# never "is it exactly 3.21.3" but "is it old enough to need the fallback". These
# three give the map file that vocabulary; see _pkgmap_one for the ordering.

# _ver_cmp <a> <b> — component-wise numeric compare, `test`-style exit status:
# 0 when a == b, 1 when a > b, 2 when a < b. Missing components count as 0, so
# 3 == 3.0 == 3.0.0 and 3.21 < 3.21.3. Each component keeps its leading digits
# only, which is what makes 15-SP5, 3.24_alpha and 2024.1-rc2 comparable at all;
# a component with no digits at the front is a 0.
_ver_cmp() {
    _vc_a=$1; _vc_b=$2
    while [ -n "$_vc_a" ] || [ -n "$_vc_b" ]; do
        _vc_x=${_vc_a%%.*}; _vc_y=${_vc_b%%.*}
        case "$_vc_a" in *.*) _vc_a=${_vc_a#*.} ;; *) _vc_a='' ;; esac
        case "$_vc_b" in *.*) _vc_b=${_vc_b#*.} ;; *) _vc_b='' ;; esac
        _vc_x=${_vc_x%%[!0-9]*}; _vc_y=${_vc_y%%[!0-9]*}
        [ -n "$_vc_x" ] || _vc_x=0
        [ -n "$_vc_y" ] || _vc_y=0
        # Leading zeros are decimal here, not octal: `[ 04 -gt 4 ]` is false,
        # which is what Ubuntu's 24.04 needs.
        [ "$_vc_x" -gt "$_vc_y" ] && return 1
        [ "$_vc_x" -lt "$_vc_y" ] && return 2
    done
    return 0
}

# _ver_match <version> <expr> — true when <version> satisfies a comparison facet
# (`<=3.20`, `<3.22`, `>=0.66`, `>13`). Anything that does not start with an
# operator is not a range and returns false, so an ordinary `name@3.20` key is
# never mistaken for one.
_ver_match() {
    _vm_v=$1
    case "$2" in
        '<='*) _vm_op=le; _vm_w=${2#'<='} ;;
        '>='*) _vm_op=ge; _vm_w=${2#'>='} ;;
        '<'*)  _vm_op=lt; _vm_w=${2#'<'} ;;
        '>'*)  _vm_op=gt; _vm_w=${2#'>'} ;;
        *)     return 1 ;;
    esac
    [ -n "$_vm_w" ] || return 1
    _ver_cmp "$_vm_v" "$_vm_w"; _vm_r=$?
    case "$_vm_op" in
        lt) [ "$_vm_r" -eq 2 ] ;;
        le) [ "$_vm_r" -ne 1 ] ;;
        gt) [ "$_vm_r" -eq 1 ] ;;
        ge) [ "$_vm_r" -ne 2 ] ;;
    esac
}

# _ver_prefixes <version> — the dotted prefixes of a version, longest first and
# excluding the version itself: 3.21.3 -> `3.21 3`. This is what lets one
# `name@3.21` row cover every 3.21.x point release, which matters because a
# distro that reports a patch level (Alpine's VERSION_ID=3.21.3) would otherwise
# need a key per point release.
_ver_prefixes() {
    _vp_v=$1
    while [ "${_vp_v%.*}" != "$_vp_v" ]; do
        _vp_v=${_vp_v%.*}
        printf '%s ' "$_vp_v"
    done
}

# --- pkgmap ------------------------------------------------------------------

# _pkgmap_re <text> — <text> escaped for use inside a BRE. Keys carry dots
# (`fzf@3.21`) and the occasional `+`; unescaped, `foo@3.21` would also match a
# row for `foo@3x21`.
_pkgmap_re() {
    printf '%s' "$1" | sed 's/[].[^$*\\+?()|{}]/\\&/g'
}

# _pkgmap_rhs <line> — the value half of a map row: everything past the first
# '=', with a trailing ` # comment` dropped (the space before # is required, so
# `a#b` survives) and both ends trimmed.
_pkgmap_rhs() {
    printf '%s' "${1#*=}" | sed 's/[[:space:]]#.*$//; s/^[[:space:]]*//; s/[[:space:]]*$//'
}

# _pkgmap_exact <key> — the RHS of the row whose key is exactly <key>, in
# <manager>.map then any.map. Non-zero when no such row exists.
_pkgmap_exact() {
    _pe_re=$(_pkgmap_re "$1")
    for _pe_map in "$OSR_LIB/pkgmap/$OSR_PKG.map" "$OSR_LIB/pkgmap/any.map"; do
        [ -f "$_pe_map" ] || continue
        _pe_line=$(grep "^[[:space:]]*${_pe_re}[[:space:]]*=" "$_pe_map" 2>/dev/null | head -n 1)
        [ -n "$_pe_line" ] || continue
        _pkgmap_rhs "$_pe_line"
        return 0
    done
    return 1
}

# _pkgmap_range <name> <version> — the RHS of the first `name@<op><ver>` row
# whose comparison holds for <version>, in <manager>.map then any.map and, within
# a file, in the order the rows are written. Ranges cannot be ordered by
# specificity the way exact keys can (`<=3.21` and `<4` are both "one row"), so
# the file order IS the tie-break: put the tightest bound first.
_pkgmap_range() {
    _pr_name=$1; _pr_ver=$2
    _pr_re=$(_pkgmap_re "$_pr_name")
    for _pr_map in "$OSR_LIB/pkgmap/$OSR_PKG.map" "$OSR_LIB/pkgmap/any.map"; do
        [ -f "$_pr_map" ] || continue
        while IFS= read -r _pr_line; do
            [ -n "$_pr_line" ] || continue
            # The facet is read with sed, not `${_pr_line%%=*}`: the '=' in `<=`
            # is not the row's separator, and cutting there would leave `<`.
            _pr_expr=$(printf '%s' "$_pr_line" \
                | sed -n "s/^[[:space:]]*${_pr_re}@\([<>]=\{0,1\}[0-9][0-9.]*\).*/\1/p")
            [ -n "$_pr_expr" ] || continue
            _ver_match "$_pr_ver" "$_pr_expr" || continue
            # Same reason: hand _pkgmap_rhs the line with the key removed, so its
            # `up to the first =` is the separator and nothing else.
            _pkgmap_rhs "${_pr_line#*"$_pr_name@$_pr_expr"}"
            return 0
        done <<RANGEROWS
$(grep "^[[:space:]]*${_pr_re}@[<>]" "$_pr_map" 2>/dev/null)
RANGEROWS
    done
    return 1
}

# _pkgmap_one <name> — echo the RHS mapped for <name>, or <name> unchanged when
# no row exists (the common case stays zero-effort, §1). Distro map wins over
# the shared any.map.
#
# Facet qualifiers (§1a): a map key may carry an optional @facet, and the most
# specific match wins:
#
#   name@trixie    codename          exact
#   name@3.21.3    version_id        exact
#   name@3.21      version_id        dotted prefix, longest first (then name@3)
#   name@<=3.21    version_id        comparison, first matching row wins
#   name@x86_64    arch              exact
#   name           -                 the bare row
#
# This is how a package's install *method* can differ by distro release
# (`lsd@jammy`, `fzf@<=3.22`) or CPU arch, not just by package manager (G6/G8).
# A qualified row exists only where that facet actually diverges, so the common
# case is still zero-effort.
_pkgmap_one() {
    _pm_name=$1
    # Facet values are empty on distros that don't report them (${VAR:+...}
    # drops the key entirely then); names + facet values carry no spaces, so the
    # unquoted expansion is safe.
    for _pm_key in \
        ${OSR_CODENAME:+"$_pm_name@$OSR_CODENAME"} \
        ${OSR_VERSION_ID:+"$_pm_name@$OSR_VERSION_ID"}; do
        if _pm_hit=$(_pkgmap_exact "$_pm_key"); then printf '%s' "$_pm_hit"; return 0; fi
    done
    if [ -n "${OSR_VERSION_ID:-}" ]; then
        for _pm_pfx in $(_ver_prefixes "$OSR_VERSION_ID"); do
            if _pm_hit=$(_pkgmap_exact "$_pm_name@$_pm_pfx"); then printf '%s' "$_pm_hit"; return 0; fi
        done
        if _pm_hit=$(_pkgmap_range "$_pm_name" "$OSR_VERSION_ID"); then
            printf '%s' "$_pm_hit"; return 0
        fi
    fi
    for _pm_key in ${OSR_ARCH:+"$_pm_name@$OSR_ARCH"} "$_pm_name"; do
        if _pm_hit=$(_pkgmap_exact "$_pm_key"); then printf '%s' "$_pm_hit"; return 0; fi
    done
    printf '%s' "$_pm_name"
}

# --- native provider ---------------------------------------------------------

# _native_installed <realpkg> — true if the package is present.
_native_installed() {
    case "$OSR_PKG" in
        apt)     dpkg -s "$1" >/dev/null 2>&1 ;;
        dnf)     rpm -q "$1" >/dev/null 2>&1 ;;
        pacman)  pacman -Q "$1" >/dev/null 2>&1 ;;
        apk)     apk info -e "$1" >/dev/null 2>&1 ;;
        xbps)    xbps-query "$1" >/dev/null 2>&1 ;;
        # portage: qlist (portage-utils) is exact + fast; portageq is always in
        # a stage3 but needs a resolvable atom, so it is the fallback.
        portage) if command -v qlist >/dev/null 2>&1; then qlist -I -e "$1" >/dev/null 2>&1
                 else portageq has_version / "$1" >/dev/null 2>&1; fi ;;
        *)       return 1 ;;
    esac
}

# _native_held <realpkg> — true if the user has held/pinned it (G2: never
# override user-defined package state).
_native_held() {
    case "$OSR_PKG" in
        apt)     apt-mark showhold 2>/dev/null | grep -qx "$1" ;;
        pacman)  grep -E '^[[:space:]]*IgnorePkg' /etc/pacman.conf 2>/dev/null | grep -qw "$1" ;;
        dnf)     grep -rl -E "^[[:space:]]*exclude=.*\b$1\b" /etc/dnf/dnf.conf /etc/yum.repos.d 2>/dev/null | grep -q . ;;
        portage) grep -rhw "$1" /etc/portage/package.mask 2>/dev/null | grep -qv '^[[:space:]]*#' ;;
        *)       return 1 ;;
    esac
}

# _xbps_clear_conflicts <realpkgs...> — remove installed packages that would
# make the xbps transaction abort, so the batch can proceed.
#
# The case this exists for: two packages are alternative implementations of one
# thing, and the one we want declares the other's name as a virtual it provides.
# `unclutter-xfixes` provides `unclutter>=0`, so on a box that already has the
# original `unclutter` xbps refuses the WHOLE transaction — the other six
# packages in the same `xbps-install` call never land, the module fails, and the
# run stops on a pointer-hiding daemon. Nothing else in the i3 rice's 238
# packages conflicts; this is about not letting one of them abort everything.
#
# xbps itself is the authority on what conflicts, not a table here: a dry run
# (-n) reports every conflict without touching the system, and its lines read
#
#   CONFLICT: unclutter-xfixes-1.6_1 with installed pkg unclutter-8_5 (matched by unclutter>=0)
#
# Only the `with installed pkg` form is actionable — the other form ("... in
# transaction") is two NEW packages disagreeing, where there is nothing
# installed to remove and no basis to pick a winner, so it falls through to the
# real install and fails there with xbps's own message.
#
# This is the one place os-rice removes a package the user may have installed,
# which G2 otherwise forbids, so it is fenced in tightly:
#   - only what xbps names as blocking THIS transaction, never a guess;
#   - never a package something else depends on (xbps-query -X): a conflict is
#     one thing, dragging a reverse-dependency closure out is another, and that
#     one is left to fail loudly with its own error;
#   - never a held/pinned package (G2 proper — that is a stated user decision);
#   - and it says both names out loud when it does it.
_xbps_clear_conflicts() {
    # A dry run needs the index, and a repo that will not sync is not this
    # function's problem — the real install is about to report it properly.
    _xc_out=$(as_root xbps-install -n "$@" 2>&1) || :
    printf '%s\n' "$_xc_out" | grep '^CONFLICT:' | grep 'with installed pkg' \
    | while read -r _xc_line; do
        # CONFLICT: <new> with installed pkg <installed> (matched by <pattern>)
        _xc_new=$(printf '%s' "$_xc_line" | awk '{print $2}')
        _xc_old=$(printf '%s' "$_xc_line" | awk '{print $6}')
        [ -n "$_xc_old" ] || continue
        # <name>-<version>_<revision> -> <name>; xbps-uhelper is in xbps itself.
        _xc_name=$(xbps-uhelper getpkgname "$_xc_old" 2>/dev/null) || _xc_name=""
        [ -n "$_xc_name" ] || continue
        if _native_held "$_xc_name"; then
            warn "$_xc_old is held - leaving it, $_xc_new cannot install"
            continue
        fi
        _xc_rev=$(xbps-query -X "$_xc_name" 2>/dev/null | grep -c .) || _xc_rev=0
        if [ "$_xc_rev" -gt 0 ]; then
            warn "$_xc_old conflicts with $_xc_new but $_xc_rev package(s) need it - leaving it"
            continue
        fi
        warn "$_xc_old conflicts with $_xc_new (same program, different implementation) - replacing it"
        as_root xbps-remove -y "$_xc_name" >/dev/null 2>&1 \
            || warn "could not remove $_xc_name - $_xc_new will fail to install"
    done
}

# _via_native <realpkgs...> — filter already-installed and held/pinned, then
# batch-install the rest. Filtering is what makes a second run all-skips (§2).
_via_native() {
    _todo=""
    for _p in "$@"; do
        if _native_installed "$_p"; then
            info "$_p already installed - skipping"
        elif _native_held "$_p"; then
            warn "$_p is held/pinned - skipping"
        else
            _todo="$_todo $_p"
        fi
    done
    [ -n "$_todo" ] || return 0
    # Refresh the package index once per run, lazily — only when we are actually
    # about to install (a fresh container/box has no lists yet).
    if [ -z "${_OSR_REFRESHED:-}" ]; then
        pkg_refresh || warn "package index refresh failed - continuing"
        _OSR_REFRESHED=1
    fi
    # Clear anything that would abort the whole xbps transaction before running
    # it, so one conflicting package cannot take the other N down with it. An
    # `if`, not `[ ] && ...`: under `set -e` a trailing false AND-list is exactly
    # the footgun that would make every non-Void install exit here.
    if [ "$OSR_PKG" = xbps ]; then
        # shellcheck disable=SC2086  # intentional word-split into a package list
        _xbps_clear_conflicts $_todo
    fi
    # shellcheck disable=SC2086  # intentional word-split into a package list
    case "$OSR_PKG" in
        # -q and Dpkg::Use-Pty=0: the step window tails a LOG FILE, and apt's
        # and dpkg's in-place progress redraws only churn it (see lib/ui.c).
        apt)     as_root env DEBIAN_FRONTEND=noninteractive \
                     apt-get install -y -q -o Dpkg::Use-Pty=0 $_todo ;;
        dnf)     as_root dnf install -y $_todo ;;
        pacman)  as_root pacman -S --needed --noconfirm $_todo ;;
        apk)     as_root apk add $_todo ;;
        xbps)    as_root xbps-install -y $_todo ;;
        # portage builds from source; --getbinpkg prefers the official binhost
        # (fast, falls back to source), --noreplace makes a rerun a no-op (§2).
        portage) as_root emerge --quiet --noreplace --getbinpkg $_todo ;;
        *)       error "no native installer for OSR_PKG='$OSR_PKG'" ;;
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
        info "$_vs_name already present (script) - skipping"
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
        info "$_vsrc_name already present (source) - skipping"
        return 0
    fi
    command -v "$_vsrc_fn" >/dev/null 2>&1 \
        || error "source builder '$_vsrc_fn' is not defined for $_vsrc_name"
    info "building $_vsrc_name from source ($_vsrc_fn)"
    "$_vsrc_fn"
    check_error $? "source build failed for $_vsrc_name"
}

# --- cargo provider (cargo install) ------------------------------------------
# Spec: cargo:<crate>  — install a Rust crate as OSR_USER into ~/.cargo/bin,
# --locked for reproducibility. Requires a toolchain, so list `rust` BEFORE any
# cargo: row in the rice/module (manifest order is the dependency graph, §4).
# Probe: the resulting binary exists in ~/.cargo/bin (§2).
_via_cargo() {
    _vca_name=$1
    _vca_crate=$2
    _vca_cargo="$OSR_HOME/.cargo/bin/cargo"
    if as_user test -x "$OSR_HOME/.cargo/bin/$_vca_name"; then
        info "$_vca_name already present (cargo) - skipping"
        return 0
    fi
    as_user test -x "$_vca_cargo" \
        || error "cargo not found for $_vca_name - install 'rust' before any cargo: package"
    # binstall first (modules/rust.sh installs it): a prebuilt-binary download
    # where upstream ships one, minutes instead of a source build. Not every
    # crate/arch has an asset, so a failure falls through to cargo install.
    if as_user test -x "$OSR_HOME/.cargo/bin/cargo-binstall"; then
        info "installing $_vca_name via cargo-binstall ($_vca_crate)"
        as_user "$_vca_cargo" binstall --no-confirm "$_vca_crate" && return 0
        warn "cargo-binstall failed for $_vca_name - falling back to a source build"
    fi
    info "installing $_vca_name via cargo ($_vca_crate)"
    as_user "$_vca_cargo" install --locked "$_vca_crate"
    check_error $? "cargo install failed for $_vca_name"
}

# --- aur provider (AUR helper: paru/yay) -------------------------------------
# Spec: aur:<pkg>  — install an AUR package via the detected helper, as OSR_USER
# (makepkg refuses to run as root). Probe: pacman -Q, since AUR packages register
# in the pacman DB just like native ones (command -v is unreliable — the binary
# name often differs from the package, e.g. visual-studio-code-insiders-bin ->
# code-insiders). paru itself is a source:provide_paru row listed BEFORE any aur:
# package (manifest order is the dependency graph, §4).

# _osr_aur_helper — echo the available AUR helper (paru preferred), or "".
# Resolved lazily at install time, not in detect.sh: paru is built mid-run, so a
# helper detected once up front would miss it.
_osr_aur_helper() {
    if command -v paru >/dev/null 2>&1; then echo paru
    elif command -v yay >/dev/null 2>&1; then echo yay
    else echo ""; fi
}

_via_aur() {
    _va_name=$1                          # logical name (skip messaging)
    _va_pkg=$2                           # real AUR package to install
    if pacman -Q "$_va_pkg" >/dev/null 2>&1; then
        info "$_va_pkg already installed (aur) - skipping"
        return 0
    fi
    _va_helper=$(_osr_aur_helper)
    [ -n "$_va_helper" ] \
        || error "no AUR helper (paru/yay) for $_va_name - install 'paru' before any aur: package"
    info "installing $_va_pkg via $_va_helper (AUR)"
    as_user "$_va_helper" -S --needed --noconfirm "$_va_pkg"
    check_error $? "AUR install failed for $_va_pkg"
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
        # shellcheck disable=SC2086  # script: intentionally word-splits into args
        case "$(_spec_method "$_rhs")" in
            native)  ;;  # already handled in pass 1
            script)  _via_script "$_name" ${_rhs#script:} ;;
            source)  _via_source "$_name" "${_rhs#source:}" ;;
            cargo)   _via_cargo  "$_name" "${_rhs#cargo:}" ;;
            aur)     _via_aur    "$_name" "${_rhs#aur:}" ;;
            *)       error "provider '${_rhs%%:*}:' not yet implemented ($_name) - covers native/script/source/cargo/aur" ;;
        esac
    done
    return 0
}

# pkg_installed <name> — true if <name> is installed under its resolved method.
pkg_installed() {
    _rhs=$(_pkgmap_one "$1")
    case "$_rhs" in
        aur:*)               pacman -Q "${_rhs#aur:}" >/dev/null 2>&1 ;;
        script:*|source:*|cargo:*) command -v "$1" >/dev/null 2>&1 ;;
        *)  for _p in $_rhs; do _native_installed "$_p" || return 1; done ;;
    esac
}

# OSR_APT_BOOTSTRAP_LISTS — the sources.list.d files os-rice writes ITSELF, to
# bootstrap a vendor repo that no Debian/Ubuntu archive carries. Currently just
# the Yandex Browser one (provide_yandex_browser, lib/build.sh).
OSR_APT_BOOTSTRAP_LISTS="/etc/apt/sources.list.d/yandex-browser.list"

# _apt_prune_bootstrap_lists — drop a bootstrap list once the vendor package
# describes the same repo itself, and do it BEFORE any apt call.
#
# Why this is not cosmetic: our list pins signed-by=/etc/apt/keyrings/*.asc, and
# the vendor's postinst writes its own list for the same URI with
# signed-by=/usr/share/keyrings/*.gpg. apt 3.0 (Debian 13+) treats a repo
# described twice with different Signed-By values as FATAL - "Conflicting values
# set for option Signed-By" / "The list of sources could not be read" - and it
# refuses to parse the WHOLE source list. So one vendor install bricks apt for
# every later module and every later apt-get the user runs by hand, with an
# error naming a browser repo that has nothing to do with what they were doing.
#
# The bootstrap list has done its job by then (it exists only to reach the first
# install; the vendor's own list keeps the package updated), and §5 says we own
# only what we wrote - which is exactly this file. The orphaned .asc key is left
# alone: harmless, and removing keys the admin may have repointed at is not ours
# to do.
_apt_prune_bootstrap_lists() {
    [ "$OSR_PKG" = apt ] || return 0
    for _pb_list in $OSR_APT_BOOTSTRAP_LISTS; do
        [ -f "$_pb_list" ] || continue
        _pb_uri=$(awk '$1 == "deb" { for (i = 2; i <= NF; i++) if ($i ~ /^https?:\/\//) { print $i; exit } }' \
            "$_pb_list" 2>/dev/null)
        [ -n "$_pb_uri" ] || continue
        # Substring match on purpose: the vendor writes the URI with a trailing
        # slash ("...deb/ stable"), and deb822 .sources files put it on a
        # URIs: line - both still contain ours.
        _pb_other=$(grep -rlF "$_pb_uri" /etc/apt/sources.list /etc/apt/sources.list.d 2>/dev/null \
            | grep -vxF "$_pb_list" | head -n 1)
        [ -n "$_pb_other" ] || continue
        info "dropping $_pb_list - $_pb_other already describes $_pb_uri (two signed-by values for one repo is fatal to apt 3.0)"
        as_root rm -f "$_pb_list"
    done
}

# pkg_refresh — refresh the package index (idempotent).
pkg_refresh() {
    case "$OSR_PKG" in
        apt)     _apt_prune_bootstrap_lists
                 as_root env DEBIAN_FRONTEND=noninteractive \
                     apt-get update -q -o Dpkg::Use-Pty=0 ;;
        dnf)     as_root dnf -q makecache ;;
        pacman)  as_root pacman -Sy --noconfirm ;;
        apk)     as_root apk update ;;
        xbps)    as_root xbps-install -S ;;
        # portage: getuto provisions the binary-package signing keyring (the
        # official binhost sets verify-signature=true) so --getbinpkg works;
        # emerge-webrsync grabs a tree snapshot (faster than rsync --sync).
        portage) command -v getuto >/dev/null 2>&1 && as_root getuto >/dev/null 2>&1 || :
                 if command -v emerge-webrsync >/dev/null 2>&1; then as_root emerge-webrsync
                 else as_root emerge --sync --quiet; fi ;;
        *)       error "no refresh verb for OSR_PKG='$OSR_PKG'" ;;
    esac
}

# pkg_remove <names...> — remove native packages (providers own their own).
# Absent packages are filtered out, not passed down: every native remover errors
# on an unknown package, which would make a first run fatal for any module that
# removes a stack it is replacing (§2 - a no-op must stay a no-op).
pkg_remove() {
    _rm=""
    for _name in "$@"; do
        _rhs=$(_pkgmap_one "$_name")
        case "$_rhs" in
            *:*) warn "pkg_remove skips non-native $_name ($_rhs)" ;;
            *)   for _p in $_rhs; do
                     if _native_installed "$_p"; then _rm="$_rm $_p"
                     else info "$_p not installed - nothing to remove"; fi
                 done ;;
        esac
    done
    [ -n "$_rm" ] || return 0
    # shellcheck disable=SC2086
    case "$OSR_PKG" in
        apt)     as_root apt-get remove -y $_rm ;;
        dnf)     as_root dnf remove -y $_rm ;;
        pacman)  as_root pacman -R --noconfirm $_rm ;;
        apk)     as_root apk del $_rm ;;
        xbps)    as_root xbps-remove -y $_rm ;;
        portage) as_root emerge --deselect --quiet $_rm && as_root emerge --depclean --quiet ;;
    esac
}

# pkg_add_repo — placeholder for the repo: provider (G1). Not in MVP.
pkg_add_repo() {
    error "pkg_add_repo (repo: provider) is not yet implemented - see DESIGN G1"
}
