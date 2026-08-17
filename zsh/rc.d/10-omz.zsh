# 10-omz.zsh — oh-my-zsh bootstrap. LAYER: dotfiles-owned, overwritten on
# update, rice-independent. Sourced before aliases (so aliases win) and before
# the 90-theme prompt.

zstyle ':omz:update' mode disabled   # os-rice manages updates, not omz

# zsh-autocomplete MUST stay last in this array. Its upstream README says to
# source it before compinit, which is impossible under omz (oh-my-zsh.sh runs
# compinit unconditionally, no opt-out) — but loading it *first* is worse: omz
# sources lib/key-bindings.zsh after it and clobbers up/down with
# up-line-or-beginning-search, killing the history menu. omz sources plugins
# after its lib/, so last-in-array is the only slot where autocomplete's own
# bindings survive. Verified: up-line-or-search / down-line-or-select.
plugins=(
    git
    zsh-autosuggestions
    zsh-syntax-highlighting
    dirhistory
    zsh-autocomplete
)

# PSReadLine-style prediction dropdown (the PowerShell profile's
# `-PredictionViewStyle ListView`). These must be set before oh-my-zsh.sh,
# because autocomplete reads them once at plugin-load time.
#
# default-context makes the live list *history* rather than completions, which
# is the half zsh-autosuggestions doesn't cover — it only does the inline ghost
# (PSReadLine's InlineView). Drop this one line to get autocomplete's stock
# completion-first list back; Tab completion is unaffected either way.
zstyle ':autocomplete:*' default-context history-incremental-search-backward
zstyle ':autocomplete:*' min-input 1  # nothing on an empty line, like ListView
zstyle -e ':autocomplete:*:*' list-lines 'reply=( $(( LINES / 3 )) )'
zstyle ':autocomplete:history-search-backward:*' list-lines 16

# autocomplete's recent-dirs module points chpwd_recent_filehandler at
# $XDG_DATA_HOME/zsh/chpwd-recent-dirs but never creates the directory — it
# zmodloads zf_mkdir and then doesn't call it — so on a machine without
# ~/.local/share/zsh every `cd` prints "no such file or directory". Create it
# here rather than at install time: rc.d ships to every machine, so this
# self-heals on boxes that won't re-run modules/zsh.sh.
_osr_zdatadir=${XDG_DATA_HOME:-$HOME/.local/share}/zsh
[ -d "$_osr_zdatadir" ] || mkdir -p "$_osr_zdatadir"
unset _osr_zdatadir

# ZSH_THEME is a no-op visually — the 90-theme layer drives the prompt (starship)
# — but oh-my-zsh still expects it set before sourcing.
ZSH_THEME="robbyrussell"

[ -r "$ZSH/oh-my-zsh.sh" ] && source "$ZSH/oh-my-zsh.sh"

# --- history: no duplicate rows ----------------------------------------------
# AFTER the omz source, which sets only hist_ignore_dups (consecutive repeats).
#
# hist_find_no_dups is the one that de-dupes the ↑ menu, and it is not optional:
# _autocomplete__history_lines branches on `[[ -o histfindnodups ]]` and only
# then strips each row's history number before comparing. Without it that
# function just truncates the list, so `cd os-rice` shows up once per run. The
# two contexts differ — the incremental list you get by typing a prefix is
# de-duped by the completion system regardless, which is why this is only
# visible on ↑.
setopt hist_find_no_dups      # collapse repeats in the ↑ menu and Ctrl-R
setopt hist_ignore_all_dups   # drop older duplicates from the list entirely
setopt hist_save_no_dups      # ...and never write them out to $HISTFILE
setopt hist_reduce_blanks     # "ls   -la" and "ls -la" are one command

# Enter runs the highlighted command straight from the menu. Without this,
# menuselect's Enter only accepts the selection onto the command line and a
# second Enter is needed to run it. The leading dot is the builtin widget, not
# autocomplete's wrapper.
bindkey -M menuselect '^M' .accept-line

# --- dropdown styling: PSReadLine ListView look ------------------------------
# AFTER the omz source on purpose, unlike the behavior styles above. omz's
# lib/completion.zsh runs `zstyle ':completion:*' list-colors ''` and autocomplete
# sets the descriptions format at init, so both silently win over anything set
# earlier in this file. Setting them here is the only thing that sticks.
#
# Every color is an ANSI SLOT NUMBER (0-15), never a hex. Each rice already themes
# those 16 slots for alacritty/foot/ghostty out of themes/<name>/theme.list, so
# the dropdown tracks the active rice for free and this file stays
# rice-independent (see the header). Hexes here would fork the palette and belong
# in 90-theme.zsh instead.

# Selected row = a full-width bar, ListView's highlighted entry. `ma` is the
# menuselect selection spec and takes raw SGR, not zsh style syntax: bg slot 8 +
# fg slot 15 + bold. That pair stays legible on light and dark rices alike, where
# a fixed 256-color grey (PSReadLine's own 48;5;238) would not.
zstyle ':completion:*' menu select
zstyle ':completion:*' list-colors 'ma=48;5;8;38;5;15;1'

# The matched portion, emphasized in place. Autocomplete's stock format prefixes a
# literal "common substring:" label; ListView has no such label, so print just the
# substring (%d) in bold accent.
zstyle ':autocomplete:*:unambiguous' format $'%B%F{4}%d%f%b'

# Group headers ("history", "commands") dimmed, the way ListView greys its
# right-hand source annotation.
zstyle ':completion:*:descriptions' format $'%F{8}%d%f'

# Inline ghost text = PSReadLine's InlinePrediction. Slot 8 is the muted role in
# every rice, matching PSReadLine's own dark-grey default. Same value as
# zsh-autosuggestions' own default — set explicitly so the intent survives an
# upstream change of mind.
ZSH_AUTOSUGGEST_HIGHLIGHT_STYLE='fg=8'

# Two things _autocomplete__history_lines gives no zstyle for, so both are done by
# rewriting the loaded function body rather than vendoring a forked copy of it.
# `autoload +X` is needed first because completion functions load lazily and
# $functions is empty until then. Every substitution degrades safely: if upstream
# changes the text, nothing matches and that tweak silently reverts to stock.
if autoload +X _autocomplete__history_lines 2>/dev/null; then
    () {
        local body=$functions[_autocomplete__history_lines]

        # 1. The typed substring inside each row, ListView's Emphasis color. The
        #    function hardcodes black-on-bright-yellow and rebuilds _comp_colors on
        #    every call, keeping only `ma=` from the caller.
        body=${body//30;103/1;38;5;6}

        # 2. Swap the leading history number ("2218  cargo run") for PSReadLine's
        #    "> " row marker. The number cannot simply be dropped at the source:
        #    it IS the row's identity here — `matches` is built by parsing it back
        #    out with ${(MS)displays##<->} to index $history, and the histfindnodups
        #    and fuzzy-sort branches both strip it with $~numpat. So leave every one
        #    of those alone and rewrite the rows only after the last consumer,
        #    immediately before compadd.
        #
        #    The _comp_colors patterns are anchored on $numpat, so they stop matching
        #    the moment the row no longer starts with a number — taking the emphasis
        #    color in (1) down with them. Rather than renumber their capture groups
        #    by hand, point that same group at whatever now sits in front: the group
        #    count, and therefore every color field after it, stays exactly as
        #    upstream wrote it. Both edits are built from $marker so the rows and the
        #    patterns cannot drift apart.
        #
        #    Re-pad to COLUMNS-1 afterwards. Upstream pads just above this point,
        #    and the rewrite runs after it, so every row would otherwise come out
        #    (numpat - marker) chars short of full width. That is not cosmetic: the
        #    `ma=` bar only covers the display string, so short rows give a bar that
        #    stops early and leaves the command line's own echo on the same terminal
        #    row, which then wraps and reads as a stray unprefixed suggestion.
        #    Then re-unique the rows, in lockstep with $matches. This is the part
        #    that is easy to miss: the number was also what kept two rows apart
        #    that are identical up to the truncation point. Strip it and e.g.
        #    `source /long/path/a.zsh` and `source /long/path/b.zsh` both truncate
        #    to the same 79 chars. compadd pairs -ld displays with -a matches by
        #    index, so a collapsed display leaves a match with no display of its
        #    own, and that orphan renders as raw text — a row with no marker, which
        #    looks like a stray unprefixed suggestion at the bottom of the ↑ menu.
        #    Dropping the duplicate row loses nothing: it was visually identical.
        local marker='> '
        body=${body//'"=(#b)${numpat}'/'"=(#b)(['${marker% }'][[:blank:]]#)'}
        body=${body/'local tag=history-lines'/'displays=( "${(@mr:COLUMNS-1:)${(@)${(@)displays##$~numpat}/#/'$marker'}}" )
  local -A _osr_seen=(); local -a _osr_d=() _osr_m=(); local -i _osr_i=
  for _osr_i in {1..$#displays}; do
    [[ -n ${_osr_seen[$displays[_osr_i]]-} ]] && continue
    _osr_seen[$displays[_osr_i]]=1
    _osr_d+=( "$displays[_osr_i]" ); _osr_m+=( "$matches[_osr_i]" )
  done
  displays=( "$_osr_d[@]" ); matches=( "$_osr_m[@]" )
  local tag=history-lines'}

        functions[_autocomplete__history_lines]=$body
    }
fi
