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
# solid block. Toned down to a plain underline, which is the only kind of change
# that keeps the syntax colors.
#
# Why not a red wash, which is what this obviously wants to be: zsh merges
# region_highlight entries by ATTRIBUTE (underline/bold/standout stack), but a
# spec carrying a COLOR replaces the whole fg/bg pair for the characters it
# covers. `root` is painted last over the entire buffer, so `bg=red` there resets
# every token's foreground and the line comes out white-on-red. Measured on a
# real zle through zsh/zpty, TERM=xterm-256color:
#
#   bg=red     -> ^[[41ml ^[[41ms ^[[39m...      41 = red bg, 39 = fg reset
#   underline  -> ^[[4m^[[32ml ^[[4m^[[32ms...   4 = underline, 32 = main's green
#
# Ordering `root` before `main` instead does not rescue it: main then overrides
# the wash on every token it colors and leaves it on the ones it does not, so the
# background comes out striped (also measured).
if (( EUID == 0 )); then
    typeset -gA ZSH_HIGHLIGHT_STYLES
    ZSH_HIGHLIGHT_STYLES[root]='underline'
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

# ssh() — terminfo safety net. Ghostty's own shell integration already wraps ssh
# (ghostty/config: shell-integration-features = ssh-env,ssh-terminfo): it installs
# xterm-ghostty into the remote's ~/.terminfo on first connect and otherwise sends
# TERM=xterm-256color. But that wrapper only exists when the integration actually
# loaded — a shell started outside Ghostty's ZDOTDIR injection (tmux, `sudo -i`,
# a nested `zsh`, an attached session) keeps TERM=xterm-ghostty with no wrapper,
# ships that name to the remote, and every curses program dies there with
#   ncurses: cannot initialize terminal type ($TERM="xterm-ghostty")
# — nano, less, htop, top, whiptail. One TERM the remote has always beats a nicer
# one it has never heard of (§9: degrade, never break).
#
# The guard is deliberately narrow: it defines this function ONLY when Ghostty's
# wrapper is absent, so where the integration works ssh-terminfo still runs and
# the remote still gets the full xterm-ghostty entry (styled underlines, true
# color). Defined here it would otherwise shadow Ghostty's — rc.d is sourced from
# ~/.zshrc, which the integration sources FIRST, so last definition wins.
# COLORTERM is what actually carries 24-bit color through to the remote, and it
# survives the downgrade.
if [[ "$TERM" == xterm-ghostty && "$GHOSTTY_SHELL_FEATURES" != *ssh-* ]]; then
  ssh() {
    TERM=xterm-256color command ssh -o "SetEnv COLORTERM=truecolor" "$@"
  }
fi
