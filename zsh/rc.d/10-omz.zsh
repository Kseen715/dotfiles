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
# The PowerShell split is reproduced across three mechanisms rather than one:
# zsh-autosuggestions' inline ghost is InlineView, ↑ opens the styled history
# menu below (ListView — everything from the "dropdown styling" block down
# applies to it), and the live list while typing stays on completions.
#
# Deliberately NO `default-context history-incremental-search-backward` here.
# Forcing the live list to history also forces TAB into the history context, so
# `cd wezt<TAB>` cannot reach wezterm/ — Tab silently stops completing anything
# that is not already in $HISTFILE. Live history is not worth a Tab key that only
# works on commands you have already run.
zstyle ':autocomplete:*' min-input 1  # nothing on an empty line, like ListView

# No trailing "./osr module zsh;" while the menu is open. Autocomplete appends an
# auto-removable `;` so you can chain a second command straight onto the one you
# picked; it never reaches the executed line, but it is visual noise on a menu
# being used to recall a single command. The style is only ever read via
# `zstyle -T` at completion time and never set at init, so it sticks from here.
zstyle ':autocomplete:*' add-semicolon no
# Live list height, and the height of the Ctrl-R list with it. A third of the
# screen was fine at 24 rows and is a wall of text at 60, so it is also capped at
# a flat $_osr_list_rows; small screens keep the old behavior, since LINES/3 is
# the smaller of the two there. -e defers the eval to completion time, so the
# parameter only has to exist by then.
typeset -gi _osr_list_rows=10
zstyle -e ':autocomplete:*:*' list-lines \
    'reply=( $(( LINES / 3 < _osr_list_rows ? LINES / 3 : _osr_list_rows )) )'
# How far ↑ can reach back, and — unavoidably — how tall the menu gets, since
# complist draws every entry it is given until the screen runs out and only then
# scrolls. That is the whole reason ↑ is handed to fzf at the end of this file:
# fzf windows its own list, so it can be ten rows deep in the screen and a
# thousand entries long at the same time, which complist cannot. This number
# still governs the fallback menu on a machine without fzf, and the Ctrl-R list
# below, so it stays: at 16 the menu stops after 16 entries no matter how long
# you hold ↑, and never draws more than 16 rows.
#
# Cost is flat until it is not — measured, keypress to rendered, on a 2461-line
# history: 16->19ms  100->16ms  200->21ms  300->29ms  500->56ms  1000->199ms
# 2000->224ms. 300 buys ~20x the reach of 16 for ~10ms, which is still under a
# frame; past ~500 it turns into a visible stall, so upstream's suggested 2000 is
# a bad trade here. For anything deeper than this, Ctrl-R searches the whole
# history and is ranked by match quality (see the sort-key patch below).
zstyle ':autocomplete:history-search-backward:*' list-lines 300

# autocomplete's recent-dirs module points chpwd_recent_filehandler at
# $XDG_DATA_HOME/zsh/chpwd-recent-dirs but never creates the directory — it
# zmodloads zf_mkdir and then doesn't call it — so on a machine without
# ~/.local/share/zsh every `cd` prints "no such file or directory". Create it
# here rather than at install time: rc.d ships to every machine, so this
# self-heals on boxes that won't re-run modules/zsh.sh.
_osr_zdatadir=${XDG_DATA_HOME:-$HOME/.local/share}/zsh
[ -d "$_osr_zdatadir" ] || mkdir -p "$_osr_zdatadir"
# --- volatile completion symlinks (WSL) --------------------------------------
# Docker Desktop symlinks /usr/share/zsh/vendor-completions/_docker into
# /mnt/wsl/docker-desktop, a mount that only exists while Docker Desktop is
# running. Open a shell with it stopped and compinit reads a dangling link:
#
#   compinit:527: no such file or directory: .../vendor-completions/_docker
#
# printed twice, because omz and zsh-autocomplete each run their own compinit.
# The link is root-owned and Docker Desktop recreates it on every start, so
# repairing it in place does not hold. Instead keep a snapshot in a user-owned
# dir placed FIRST in $fpath: compinit's scan loop indexes by basename and skips
# every later file of a name it has already seen ($_i_test), so the dangling
# link is never read - and docker completion keeps working while Docker Desktop
# is down, rather than merely failing quietly.
#
# Only links resolving under /mnt are copied. Everything else in $fpath is on a
# real filesystem and cannot vanish mid-session, and each copy is a permanent
# shadow, so this stays as narrow as the problem. Cost is one glob over $fpath,
# ~1 ms: a readdir per dir, then an lstat only on the handful of matches.
_osr_compdir=$_osr_zdatadir/completions
[ -d "$_osr_compdir" ] || mkdir -p "$_osr_compdir"
() {
    local link target snap
    for link in ${^fpath}/_*(N@); do
        # A broken link :A-resolves to itself, so this drops it: the snapshot
        # taken while the mount was up is what covers that case.
        target=${link:A}
        [[ $target == /mnt/* && -r $target ]] || continue
        snap=$_osr_compdir/${link:t}
        # No `-e $snap ||` shortcut: unlike POSIX test, zsh's -nt is FALSE when
        # the older file does not exist at all, which is exactly the first run.
        [[ -e $snap && ! $target -nt $snap ]] && continue
        cp -f -- "$target" "$snap" 2>/dev/null
    done
}
# Guarded, not unconditional: 10-omz.zsh is re-sourceable and fpath is not -U.
(( $fpath[(I)$_osr_compdir] )) || fpath=("$_osr_compdir" $fpath)
unset _osr_compdir _osr_zdatadir

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

# ...and the cursor keys move the cursor instead of walking the menu. Two
# separate failures are being fixed here, both verified by binding one side at a
# time and watching which combination actually works.
#
# 1. ←/→ ARE bound in menuselect, to menuselect's own backward-char/forward-char,
#    which move the highlight — so after recalling a line there is no way to edit
#    it in place. A key bound to a widget menuselect does not implement instead
#    accepts the selection, leaves the menu and runs that widget, so the leading
#    dot (builtin widget, not autocomplete's wrapper) is what does the work.
#
# 2. Home/End/Delete are NOT bound in menuselect at all, and each starts with a
#    sequence that IS a live prefix there (^[[ and ^[O both lead to the arrows).
#    Zsh reads the prefix, finds no complete match and DISCARDS the lot, so the
#    key does nothing at all — it never even reaches the fallthrough Backspace
#    gets (^? is a prefix of nothing, which is why that one already works). The
#    menuselect binding below exists only to make each sequence a COMPLETE match
#    and stop the swallowing.
#
# The widget that RUNS, in both cases, comes from the MAIN keymap: complist hands
# an unhandled widget back to it. Hence every key goes into both keymaps — and
# hence the main-keymap side can be a wrapper (see below) while menuselect keeps
# the plain builtin.
#
# omz's lib/key-bindings.zsh binds Home/End from terminfo khome/kend, which under
# TERM=xterm-256color is the application-mode pair (^[OH/^[OF) and nothing else. A
# terminal sending the CSI or vt220 form — foot, alacritty and wezterm all can, and
# so does xterm outside application mode — lands on an unbound key. Bind the lot;
# they are unambiguous and unused otherwise.

# Nothing wraps these widgets. An earlier attempt routed them through a wrapper
# that called the BUILTIN widget, and that silently broke a zsh-autosuggestions
# feature: it makes end-of-line and forward-char *accept* widgets, so End and →
# complete the ghost suggestion — and going around them via the leading dot killed
# that. Caught by diffing against a stock shell, which accepted where the patched
# one did not. The plain widget names below keep stock behavior; the stale-ghost
# problem they were added for is handled in the hook patch further down instead.

() {
    local -A osr_keys=(
        '^[[D'  backward-char      '^[OD'  backward-char
        '^[[C'  forward-char       '^[OC'  forward-char
        '^[[H'  beginning-of-line  '^[OH'  beginning-of-line
        '^[[1~' beginning-of-line  '^[[7~' beginning-of-line
        '^[[F'  end-of-line        '^[OF'  end-of-line
        '^[[4~' end-of-line        '^[[8~' end-of-line
        '^[[3~' delete-char
    )
    local osr_k osr_w
    for osr_k in ${(k)osr_keys}; do
        osr_w=$osr_keys[$osr_k]
        bindkey               "$osr_k"  "$osr_w"
        bindkey -M menuselect "$osr_k" ".$osr_w"
    done
}

# Backspace is deliberately NOT in that list. It already works (see above), and
# leaving it unbound in menuselect keeps complist's own handling of it while the
# in-menu text search (^R/^S) is active, where it edits the search string.

# --- leaving the menu must not re-open the live completion list ---------------
# The keys above hand control back to the main keymap, and autocomplete's
# line-pre-redraw hook then re-runs its live list at the new cursor position. On a
# recalled history line the cursor lands on a word, so what you get is a COMMAND
# completion — and under WSL, where $PATH carries the /mnt/c interop dirs, that is
# a screenful of Windows .exe/.dll names dumped under a line you only meant to
# edit.
#
# Not a new problem: Backspace has always left the menu and done exactly this. But
# it was rare when Backspace was the only key that could leave, and ←/→/Home/End
# make it happen on every single edit, so it gets fixed here rather than lived
# with.
#
# The rule below is: CURSOR MOVEMENT alone never re-lists, while anything that
# edits the buffer still does — so the live list keeps working as you type.
# Verified both ways: Home/←/End on a recalled line leave a clean prompt, and
# Backspace on that same line still lists.
#
# `zstyle ':autocomplete:<widget>:' ignore yes` is upstream's own knob for exactly
# this and it is NOT enough on its own — measured, not assumed. By the time the
# movement widget runs, the list has already been drawn by an earlier asynchronous
# pass, and `ignore` only suppresses a *new* one, leaving the stale list on screen.
# The pending job has to be cancelled and the display cleared as well, which no
# style exposes; hence a body rewrite, like the one at the end of this file.
# Degrades safely: if upstream renames the function, $functions comes back empty
# and the whole block is skipped.
() {
    local body=$functions[.autocomplete:async:complete]
    [[ -n $body ]] || return 0
    functions[.autocomplete:async:complete]='if [[ ${LASTWIDGET##.} == (up-line-or-search|down-line-or-select|menu-select|history-search-backward|(|reverse-)menu-complete) ]]; then
    unset POSTDISPLAY
  fi
  if [[ ${LASTWIDGET##.} == (beginning-of-line|end-of-line|backward-char|forward-char|backward-word|forward-word) ]]; then
    z-async cancel complete
    z-async cancel wait
    builtin zle -Rc
    return 0
  fi
'$body
}

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

# No scroll indicator. autocomplete's own config sets
#   ':completion:*:default' select-prompt '%F{black}%K{12}line %l %p%f%k'
# which draws a `line 293/293 Bottom` bar under a list that is taller than the
# screen — except each scroll step paints a NEW one without erasing the last, so
# scrolling a long history menu leaves a stack of them at different positions.
# The drawing is complist's own C code, not a shell function, so there is nothing
# to patch the way the functions above are patched; dropping the style is the only
# lever rc.d has. `-d` deletes rather than sets it to empty, which would still
# reserve the line. Safe to place here: autocomplete sets it once at plugin load,
# and unlike `menu` and `list-prompt` it is not re-applied by its precmd hook.
zstyle -d ':completion:*:default' select-prompt

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

# Three things _autocomplete__history_lines gives no zstyle for, all done by
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
        #    The rewrite must NOT be assigned straight back into $displays, and this
        #    is the subtle part. Upstream declares `local -aU displays` — a
        #    unique-constrained array. While every row still carries its history
        #    number they are all distinct, so the constraint never fires. Strip the
        #    number and rows that are identical up to the truncation point collapse
        #    (`source /long/path/a.zsh` and `.../b.zsh` truncate to the same 79
        #    chars), so the moment the stripped list is assigned, zsh SILENTLY drops
        #    one element — leaving $displays one shorter than $matches, which
        #    compadd pairs by index. Everything after the collapse is then off by
        #    one: the highlighted row and the command the menu inserts disagree.
        #
        #    So build the rows in a plain array, de-duplicate in lockstep with
        #    $matches while the two are still aligned, and only then assign back —
        #    by which point the values are unique and -U has nothing to remove.
        #    Padding happens before the uniqueness test because truncation is what
        #    creates the collision. Dropping the duplicate row loses nothing: it was
        #    visually identical.
        # 3. Ctrl-R ranking: stop recency from outvoting match quality. The sort key
        #    is `HISTNO - num + 64*$#match[3] + 16*mbegin[3] + 4*$#match[1]`, lowest
        #    first — recency plus a penalty for a longer, later-starting match. The
        #    quality half maxes out in the low hundreds, but recency is added raw and
        #    scales with the whole history, so past a few hundred commands it decides
        #    the order on its own. Measured with `cat` on a 2461-line history:
        #
        #      cat ~/.ssh/id_hosting   span 3 @1 (perfect)  recency 382  -> 674
        #      deactivate              span 6 @4 (c..a..t)  recency  57  -> 545
        #
        #    so a sloppy subsequence match won purely by being 325 commands newer.
        #    Dividing recency by 8 keeps it as a tie-breaker between comparable
        #    matches while letting a prefix match win: the same two become 255 and
        #    455. Raise the divisor to favour quality harder, lower it for recency.
        body=${body/'HISTNO - num + 64 * $#match[3]'/'(HISTNO - num) / 8 + 64 * $#match[3]'}

        local marker='> '
        body=${body//'"=(#b)${numpat}'/'"=(#b)(['${marker% }'][[:blank:]]#)'}
        body=${body/'local tag=history-lines'/'local -A _osr_seen=(); local -a _osr_d=() _osr_m=()
  local -i _osr_i=; local _osr_t=
  for _osr_i in {1..$#displays}; do
    _osr_t="${(mr:COLUMNS-1:)${${displays[_osr_i]##$~numpat}/#/'$marker'}}"
    [[ -n ${_osr_seen[$_osr_t]-} ]] && continue
    _osr_seen[$_osr_t]=1
    _osr_d+=( "$_osr_t" ); _osr_m+=( "$matches[_osr_i]" )
  done
  displays=( "$_osr_d[@]" ); matches=( "$_osr_m[@]" )
  local tag=history-lines'}

        functions[_autocomplete__history_lines]=$body
    }
fi

# --- ↑ history styling: the same ListView the dropdown above draws ------------
# ANSI slot numbers only, never hexes, for the reason in the dropdown block: the
# rice themes those 16 slots, so this tracks the active rice for free.
#
#   fg+/bg+  the selected row  -> slot 15 on slot 8, bold: `list-colors ma=...`
#   hl/hl+   the matched part  -> slot 4 bold: the `unambiguous` format above
#   prompt   the search line   -> slot 8, like `descriptions`
#
# Everything fzf would otherwise draw is off — info line, separator, scrollbar,
# gutter, pointer, marker — because the menu has none of them either. The '> '
# on each row is put there by the feed, not by fzf's pointer, so it is on EVERY
# row exactly like the marker the history-lines patch above adds; the only thing
# marking the selection is the highlight bar.
typeset -g _osr_fzf_colors='fg:-1,bg:-1,gutter:-1,query:-1,pointer:-1'
_osr_fzf_colors+=',fg+:15,bg+:8:bold,hl:4:bold,hl+:4:bold,prompt:8'

# --- ↑ history: an fzf window, ten rows deep, the whole history long ----------
# complist has no way to be a *window*: it draws every match it is given until
# the screen runs out, so the ↑ menu is only ever as short as it is shallow. The
# depth limit above trades one for the other, and the way around it — lying to
# zsh about $LINES so complist thinks the screen is twelve rows — was built,
# measured, and thrown away; see the note at the end of this file, and do not
# rebuild it.
#
# fzf does its own windowing against the real terminal, which is the entire
# point: --height reserves ten rows and scrolls the rest inside them, so the
# list is short on screen and still reaches back through every command ever run.
#
# NO KEY CHANGES ANYWHERE. ↑ still opens it (the widget NAME up-line-or-search is
# taken over, so ↑, ^P and vi `k` all keep working, and autosuggestions' rebind
# at first precmd wraps this rather than replacing it — verified by reading back
# $widgets afterwards). Inside, every key does what it did in the menuselect
# block above: Enter runs the highlighted command outright, ←/→/Home/End accept
# it onto the command line and leave the cursor where that key would have put it,
# Backspace edits the search, ↑/↓ walk the list, Esc and ^C leave the line
# exactly as it was.
#
# Layout is fzf's default (bottom-up) on purpose: the newest command sits against
# the search line and ↑ walks backwards in time, which is the direction the key
# already means. --layout=reverse would invert that and silently redefine ↑.
if (( $+commands[fzf] )) &&
        [[ ${widgets[up-line-or-search]} == user:* && -z ${widgets[_osr_up-line-or-search]} ]]; then

    # One row per entry, newest first, duplicates dropped, and multi-line
    # commands folded onto a single row with their newlines shown as \n — the
    # same shape the ListView menu gives them. The real command is never carried
    # in the display: the row is <history number>\t<display>, --with-nth hides
    # the number, and accepting looks the number back up in $history, so a
    # recalled function body comes back with its real newlines intact.
    #
    # $1 is the prefix to keep, and it is filtered HERE rather than handed to fzf
    # as an anchored `^prefix` query. Anchoring cannot work: fzf matches against
    # what it displays, every row displays the leading '> ' marker, so `^cargo`
    # anchors against "> cargo …" and matches nothing at all — which is exactly
    # how typing a word and pressing ↑ came to return an empty window. Filtering
    # the feed also keeps the semantics ↑ has always had here: entries that BEGIN
    # with what was typed, not entries that fuzzily contain it. The fzf query is
    # left empty so typing narrows within that set.
    #
    # Blank entries are dropped. A line that was only whitespace is still a
    # history entry, and it renders as a bare marker with nothing after it.
    #
    # (kOn) is keys, numeric, descending. Streamed, not buffered: fzf is up and
    # drawing while this is still walking $history, so the ~6 ms this takes over
    # 1100 entries (measured) is never waited on.
    _osr_fzf_history_feed() {
        local -A seen
        local k cmd
        local prefix=$1
        for k in ${(kOn)history}; do
            cmd=$history[$k]
            [[ -z ${cmd//[[:space:]]/} ]] && continue
            # Substring compare, not $cmd != $prefix*, so that a prefix with a
            # glob character in it (`ls *.c` and friends) is matched literally.
            [[ -n $prefix && ${cmd[1,$#prefix]} != "$prefix" ]] && continue
            [[ -n ${seen[$cmd]-} ]] && continue
            seen[$cmd]=1
            print -r -- "$k"$'\t'"> ${cmd//$'\n'/\\n}"
        done
    }

    zle -A up-line-or-search _osr_up-line-or-search
    _osr_fzf_history() {
        # ↑ inside a multi-line buffer moves up a line and always has. That is
        # autocomplete's own first branch, so hand those straight back to it.
        if [[ $LBUFFER == *$'\n'* ]]; then
            zle _osr_up-line-or-search
            return
        fi
        setopt localoptions pipefail no_aliases noglobsubst
        local out key idx
        # --no-sort keeps the feed's order — newest first — even while the query
        # narrows, so this stays a history list rather than turning into a
        # relevance ranking that moves rows around under the cursor as you type.
        #
        # FZF_DEFAULT_OPTS is cleared for this one call: it is a user-level knob
        # for interactive fzf, and a --layout or --bind left in it would rewrite
        # the keys this widget promises not to change.
        out=$( _osr_fzf_history_feed "$LBUFFER" |
            FZF_DEFAULT_OPTS= FZF_DEFAULT_OPTS_FILE= fzf \
            --height="~$(( _osr_list_rows + 1 ))" --min-height=3 \
            --delimiter=$'\t' --with-nth='2..' \
            --no-multi --no-sort \
            --no-info --no-separator --no-scrollbar \
            --pointer=' ' --marker=' ' --gutter=' ' --prompt='> ' \
            --color="$_osr_fzf_colors" \
            --expect=left,right,home,end ) || { zle reset-prompt; return 0 }

        # --expect puts the key that ended it on the first line, empty for Enter.
        key=${out%%$'\n'*}
        idx=${${out#*$'\n'}%%$'\t'*}
        [[ -n $idx ]] && BUFFER=$history[$idx]
        case $key in
        ( home ) CURSOR=0 ;;
        ( left ) (( CURSOR = $#BUFFER - 1 > 0 ? $#BUFFER - 1 : 0 )) ;;
        ( * )    CURSOR=$#BUFFER ;;
        esac
        # fzf scrolled the terminal out from under zle, so the prompt has to be
        # redrawn before anything else happens — including before accept-line,
        # which would otherwise run the command over a stale line.
        zle reset-prompt
        [[ -z $key ]] && zle accept-line   # Enter: run it, as it does in the menu
        return 0
    }
    zle -N _osr_fzf_history
    zle -N up-line-or-search _osr_fzf_history
fi

# --- rejected: capping the ↑ menu's height with a fake $LINES -----------------
# Recorded so it is not tried again, now that the block above solves it properly.
# complist exposes no style for the menu's height — not `menu`, `list-prompt`,
# `select-prompt` or `select-scroll` — and autocomplete's `list-lines` reaches
# the live list and the depth limit only. The single lever is the height complist
# reads, $LINES, so a wrapper around up-line-or-search set it to 12 for the
# duration of the menu and put it back on the way out.
#
# It worked in the narrow sense — ten rows, scrolling intact, $LINES honest again
# by the time anything else looked at it, the tty itself never touched — and it
# was still wrong, in two ways only a real terminal shows:
#
#  - Leftovers. zsh fills the screen it believes it has and expects the terminal
#    to scroll at the bottom of it. Below a fake bottom that scroll never
#    happens, so the rows the menu drew over — most visibly a recalled multi-line
#    function — stayed on screen after the menu was gone.
#  - Ghostty closed the window outright while scrolling. Never reproduced under
#    the pty harness it was developed against, which is the point: the failure
#    mode of lying to zsh about the screen depends on the terminal, so passing
#    every test here proves nothing about the terminal actually in use.
#
# A shell that can lose its window to a held-down arrow key is not worth ten rows
# of screen.
