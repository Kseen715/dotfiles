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

# The `root` highlighter above paints the WHOLE buffer whenever EUID is 0, and
# its default style is `standout` — reverse video, which turns the line into one
# solid block and swaps every syntax color into a background. Keep the warning,
# drop the block: a wash of the theme's own red behind the line says "root" just
# as loudly and leaves the colors on top readable.
#
# Slot names, not hexes, for the reason the fzf colors in 10-omz.zsh spell out:
# the rice themes the 16 ANSI slots, so `red` here is whatever red the active
# rice defines and tracks a rice switch for free.
#
# ZSH_HIGHLIGHT_STYLES is declared by the plugin, which omz loads before this
# file; the typeset is here so an out-of-order source cannot turn the assignment
# into an arithmetic subscript on an undeclared name.
if (( EUID == 0 )); then
    typeset -gA ZSH_HIGHLIGHT_STYLES
    ZSH_HIGHLIGHT_STYLES[root]='bg=red'
    # One color has to move out of the way: main paints an unknown command
    # `fg=red,bold`, the same slot as the wash, so a mistyped command would be
    # red on red — invisible, which is exactly when you least want it. Bright red
    # (slot 9) stays inside the rice palette and reads on top of slot 1.
    ZSH_HIGHLIGHT_STYLES[unknown-token]='fg=9,bold'
fi

# cargo() — route `cargo install` and `cargo install-update` through
# cargo-binstall (prebuilt binaries instead of source builds), each with a
# source-build fallback for crates binstall cannot resolve. install-update gets
# it via the shim os-rice modules/rust.sh installs from dotfiles/cargo/; without
# the shim, or without binstall, every branch is a plain passthrough.
cargo() {
  local shim=~/.local/bin/cargo-binstall-shim
  if [[ $1 == install-update && -x $shim ]] && (( $+commands[cargo-binstall] )); then
    command cargo install-update -r $shim "${@:2}"
  elif [[ $1 == install && "${*:2}" != *--path* && "${*:2}" != *--git* ]] && (( $+commands[cargo-binstall] )); then
    # binstall compiles from source itself when a crate has no prebuilt binary,
    # but bails outright when it cannot resolve the crate at all ("wezterm is not
    # found") — hence the fallback to a real source install.
    # --path/--git builds are local sources binstall can't fetch: passthrough.
    command cargo binstall --no-confirm "${@:2}" || command cargo "$@"
  else
    command cargo "$@"
  fi
}

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
