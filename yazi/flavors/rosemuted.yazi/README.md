# rosemuted.yazi

Yazi flavor for the **void-i3-rosemuted** rice — minimal, terminal-native.

It is byte-identical to `xin.yazi` on purpose: both are written against ANSI
color *names* rather than hex, so the file manager renders in whatever palette
the terminal is currently running. For this rice that palette is the rosemuted
one (`config/ghostty/ghostty-theme`, `config/wezterm/wezterm-theme.toml`,
`config/Xresources/colors` — all the same 16 colors), so yazi is themed by the
rice without carrying a second copy of the hex values that would then drift.

Mapping: accents → `cyan`/`blue`/`green`/`yellow`/`red`/`magenta`,
backgrounds → `reset`/`black`/`darkgray`, foregrounds → `white`/`gray`.

Selected by `os-rice/rices/void-i3-rosemuted/config/yazi/theme.toml`.
Code-preview syntax highlighting falls back to yazi's built-in theme.
