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
    # Fix root-owned files from a previous sudo run (§8): git operations run as
    # OSR_USER, so every byte under the repo must be owned by that user.
    if [ -d "$_gr_dir" ]; then
        as_root chown -R "$OSR_USER:$OSR_USER" "$_gr_dir" 2>/dev/null || true
    fi
    if [ -d "$_gr_dir/.git" ]; then
        _gr_remote=$(as_user git -C "$_gr_dir" remote get-url origin 2>/dev/null || echo "")
        if [ "$_gr_remote" = "$_gr_url" ] || [ "$_gr_remote" = "$_gr_url.git" ] || [ "$_gr_remote" = "${_gr_url%.git}" ]; then
            if ! as_user git -C "$_gr_dir" diff --quiet 2>/dev/null || ! as_user git -C "$_gr_dir" diff --cached --quiet 2>/dev/null; then
                info "$_gr_name has local changes - resetting to clean state"
                as_user git -C "$_gr_dir" reset --hard HEAD >/dev/null 2>&1
                as_user git -C "$_gr_dir" clean -fd >/dev/null 2>&1
            fi
            as_user git -C "$_gr_dir" pull --ff-only
            check_error $? "failed to update $_gr_name"
        else
            info "$_gr_name points at a different remote - recloning"
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
#
# The presence probe is the FILE, not the directory: ~/.oh-my-zsh can exist
# while holding no oh-my-zsh at all. install_zsh_plugin above creates
# custom/plugins/ inside it, and on a distro that packages omz system-wide
# (Armbian ships /etc/oh-my-zsh and exports ZSH=/etc/oh-my-zsh from its stock
# ~/.zshrc) nothing else ever writes a core there. A directory probe called that
# stub "already installed", so the core never landed, `source $ZSH/oh-my-zsh.sh`
# in 10-omz.zsh found nothing to read, and the whole plugin list - highlighting,
# autosuggestions, autocomplete, and with it the ↑ history widget - silently did
# not load.
install_omz() {
    if [ -r "$OSR_HOME/.oh-my-zsh/oh-my-zsh.sh" ]; then
        info "oh-my-zsh already installed - skipping"
        return 0
    fi
    # The stub case. Upstream's installer refuses to write into an existing
    # $ZSH directory, so seed the core with a plain clone and carry the existing
    # custom/ across - that is where install_zsh_plugin put the three plugins,
    # and re-cloning them would be the slow way to end up in the same place.
    if [ -d "$OSR_HOME/.oh-my-zsh" ]; then
        _omz_new="$OSR_HOME/.oh-my-zsh.osr-new"
        as_user rm -rf "$_omz_new"
        as_user git clone --depth 1 https://github.com/ohmyzsh/ohmyzsh.git "$_omz_new"
        check_error $? "failed to clone oh-my-zsh"
        if [ -d "$OSR_HOME/.oh-my-zsh/custom" ]; then
            as_user rm -rf "$_omz_new/custom"
            as_user mv "$OSR_HOME/.oh-my-zsh/custom" "$_omz_new/custom"
        fi
        as_user rm -rf "$OSR_HOME/.oh-my-zsh"
        as_user mv "$_omz_new" "$OSR_HOME/.oh-my-zsh"
        return 0
    fi
    # Patch out the installer's interactive bits (launch zsh / chsh) so it stays
    # non-interactive and re-runnable.
    _omz_script=$(osr_fetch_stdout https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh \
        | sed 's:env zsh -l::g; s:chsh -s .*$:true:g')
    printf '%s' "$_omz_script" | as_user sh -s -- "" --unattended --skip-chsh
    check_error $? "failed to install oh-my-zsh"
}
