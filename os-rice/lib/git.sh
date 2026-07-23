# lib/git.sh — git clone/update + oh-my-zsh helpers (POSIX sh)
#
# Single copy of logic the old per-distro modules pasted (and drifted) — the
# arch zsh.sh dropped its check_error calls hand-rolling this (§Current state).
# All git work runs as OSR_USER (user-space, §8).

# install_or_update_git_repo <name> <url> <dir> [clone-args...] — clone if
# absent; if present and the remote matches, reset-if-dirty then pull; if the
# remote differs, re-clone. Idempotent (§2).
install_or_update_git_repo() {
    _gr_name=$1; _gr_url=$2; _gr_dir=$3; shift 3
    if [ -d "$_gr_dir/.git" ]; then
        _gr_remote=$(as_user git -C "$_gr_dir" remote get-url origin 2>/dev/null || echo "")
        if [ "$_gr_remote" = "$_gr_url" ] || [ "$_gr_remote" = "$_gr_url.git" ] || [ "$_gr_remote" = "${_gr_url%.git}" ]; then
            if ! as_user git -C "$_gr_dir" diff --quiet 2>/dev/null || ! as_user git -C "$_gr_dir" diff --cached --quiet 2>/dev/null; then
                info "$_gr_name has local changes — resetting to clean state"
                as_user git -C "$_gr_dir" reset --hard HEAD >/dev/null 2>&1
                as_user git -C "$_gr_dir" clean -fd >/dev/null 2>&1
            fi
            as_user git -C "$_gr_dir" pull --ff-only
            check_error $? "failed to update $_gr_name"
        else
            info "$_gr_name points at a different remote — recloning"
            rm -rf "$_gr_dir"
            as_user git clone "$@" "$_gr_url" "$_gr_dir"
            check_error $? "failed to clone $_gr_name"
        fi
    else
        as_user git clone "$@" "$_gr_url" "$_gr_dir"
        check_error $? "failed to clone $_gr_name"
    fi
}

# install_zsh_plugin <name> <url> — clone/update an oh-my-zsh custom plugin.
install_zsh_plugin() {
    _zp_dir="$OSR_HOME/.oh-my-zsh/custom/plugins/$1"
    install_or_update_git_repo "$1" "$2" "$_zp_dir" --depth 1
}

# install_omz — install oh-my-zsh unattended if absent (§7 G5: it is an
# installed program, not config — one install method, never vendored).
install_omz() {
    if [ -d "$OSR_HOME/.oh-my-zsh" ]; then
        info "oh-my-zsh already installed — skipping"
        return 0
    fi
    # Patch out the installer's interactive bits (launch zsh / chsh) so it stays
    # non-interactive and re-runnable.
    _omz_script=$(osr_fetch_stdout https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh \
        | sed 's:env zsh -l::g; s:chsh -s .*$:true:g')
    printf '%s' "$_omz_script" | as_user sh -s -- "" --unattended --skip-chsh
    check_error $? "failed to install oh-my-zsh"
}
