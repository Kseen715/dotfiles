# 00-env.zsh — user/machine environment. LAYER: seeded once if absent, then
# never touched by os-rice. Edit this freely; it is yours.
#
# PATH edits are guard-style (§5): a re-source never duplicates an entry.

export ZSH="$HOME/.oh-my-zsh"

# Guards return 0 unconditionally: a missing dir is normal, never an error
# (important if this file is ever sourced under `set -e`).
_osr_path_prepend() { case ":$PATH:" in *":$1:"*) return 0 ;; esac; [ -d "$1" ] && PATH="$1:$PATH"; return 0; }
_osr_path_append()  { case ":$PATH:" in *":$1:"*) return 0 ;; esac; [ -d "$1" ] && PATH="$PATH:$1"; return 0; }

# Under zsh, keep $path unique for good: the guards below cover this file, but
# `brew shellenv` (further down) prepends unconditionally and would otherwise
# duplicate brew's bin dirs. Guarded so a POSIX sh sourcing this file still works.
[ -n "${ZSH_VERSION:-}" ] && typeset -U path PATH

# ~/.local/bin holds the small binaries os-rice compiles (osrvv for the starship
# prompt, lcc). Debian/Ubuntu add it from ~/.profile, which is bash-login only
# and zsh never reads, so it has to be here.
_osr_path_prepend "$HOME/.local/bin"
_osr_path_prepend "$HOME/.cargo/bin"
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

# Homebrew shell environment (machine-specific), only if installed. `brew
# shellenv` prepends brew's bin dirs unconditionally, so: this block is the ONLY
# place they enter PATH (adding them above too is what duplicated the entry), and
# the HOMEBREW_PREFIX guard makes a re-source a no-op.
#
# Probed by absolute path, never `command -v brew`. A PATH lookup that MISSES has
# to stat every entry, and under WSL interop $PATH carries ~25 /mnt/c Windows
# dirs: one failed lookup measured 44.5 ms, which was the whole cost of this file
# on a box without brew. The three prefixes below are where brew actually installs
# (linuxbrew, macOS ARM, macOS Intel); an install anywhere else needs
# HOMEBREW_PREFIX exported, or its own line in 99-local.zsh.
if [ -z "${HOMEBREW_PREFIX:-}" ]; then
    for _osr_brew in /home/linuxbrew/.linuxbrew/bin/brew /opt/homebrew/bin/brew /usr/local/bin/brew; do
        [ -x "$_osr_brew" ] || continue
        eval "$("$_osr_brew" shellenv)"
        break
    done
    unset _osr_brew
fi

# Keep the distro's .pc files findable. Brew's pkgconf (installed as a pyenv
# build dep) shadows /usr/bin/pkg-config and searches ONLY brew's prefix, so any
# source build fails on system libs that are installed - wayland-client, gtk4.
# `brew unlink pkgconf` fixes it until the next `brew upgrade` relinks it; this
# loop is the belt-and-braces. Guard-style like the PATH edits above: a
# re-source never duplicates an entry, and it is a no-op for the system
# pkg-config (these dirs are already its defaults).
for _pc in "/usr/lib/$(uname -m)-linux-gnu/pkgconfig" /usr/lib/pkgconfig /usr/share/pkgconfig; do
    [ -d "$_pc" ] || continue
    case ":${PKG_CONFIG_PATH:-}:" in *":$_pc:"*) continue ;; esac
    PKG_CONFIG_PATH="${PKG_CONFIG_PATH:+$PKG_CONFIG_PATH:}$_pc"
done
unset _pc
export PKG_CONFIG_PATH
