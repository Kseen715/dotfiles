# 00-env.zsh — user/machine environment. LAYER: seeded once if absent, then
# never touched by os-rice. Edit this freely; it is yours.
#
# PATH edits are guard-style (§5): a re-source never duplicates an entry.

export ZSH="$HOME/.oh-my-zsh"

# Guards return 0 unconditionally: a missing dir is normal, never an error
# (important if this file is ever sourced under `set -e`).
_osr_path_prepend() { case ":$PATH:" in *":$1:"*) return 0 ;; esac; [ -d "$1" ] && PATH="$1:$PATH"; return 0; }
_osr_path_append()  { case ":$PATH:" in *":$1:"*) return 0 ;; esac; [ -d "$1" ] && PATH="$PATH:$1"; return 0; }

_osr_path_prepend "$HOME/.cargo/bin"
_osr_path_prepend "/home/linuxbrew/.linuxbrew/bin"
_osr_path_append  "/usr/local/go/bin"
export GOPATH="$HOME/go"
_osr_path_append  "$GOPATH/bin"
_osr_path_append  "$HOME/.nvm"
_osr_path_append  "$HOME/.npm"
export PATH

unset -f _osr_path_prepend _osr_path_append

export EDITOR=micro
export STEAM_FORCE_DESKTOPUI_SCALING=1
export STARSHIP_LOG=error  # silence scan_timeout warns without slowing scans

# Homebrew shell environment (machine-specific), only if installed.
if command -v brew >/dev/null 2>&1; then
    eval "$(brew shellenv)"
fi
