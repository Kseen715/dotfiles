# 20-aliases.zsh — personal aliases + functions. LAYER: dotfiles-owned,
# overwritten on update, rice-independent. Sourced after oh-my-zsh so these win.

if command -v lsd >/dev/null 2>&1; then
    alias ls="lsd"
fi
alias la="ls -lah"
alias ll="ls -l"
alias git-graph="git log --graph"
alias stfu='f() { nohup $@ & };f'
alias s='f() { sudo $@ };f'

ZSH_HIGHLIGHT_HIGHLIGHTERS=(main brackets pattern cursor root)

# y() — yazi wrapper that cd's to the dir you quit in.
y() {
    local tmp cwd
    tmp="$(mktemp -t "yazi-cwd.XXXXXX")"
    yazi "$@" --cwd-file="$tmp"
    if cwd="$(command cat -- "$tmp")" && [ -n "$cwd" ] && [ "$cwd" != "$PWD" ]; then
        builtin cd -- "$cwd"
    fi
    rm -f -- "$tmp"
}
