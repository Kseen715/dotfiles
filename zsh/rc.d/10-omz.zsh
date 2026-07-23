# 10-omz.zsh — oh-my-zsh bootstrap. LAYER: dotfiles-owned, overwritten on
# update, rice-independent. Sourced before aliases (so aliases win) and before
# the 90-theme prompt.

zstyle ':omz:update' mode disabled   # os-rice manages updates, not omz

plugins=(
    git
    zsh-autosuggestions
    zsh-syntax-highlighting
    dirhistory
)

# ZSH_THEME is a no-op visually — the 90-theme layer drives the prompt (starship)
# — but oh-my-zsh still expects it set before sourcing.
ZSH_THEME="robbyrussell"

[ -r "$ZSH/oh-my-zsh.sh" ] && source "$ZSH/oh-my-zsh.sh"
