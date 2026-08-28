#!/bin/sh
# Proves the Linux modules written in C (the POSIX branch of modules/*.c, run by
# `osr module run <name>`) do what the shell modules they replaced did, frozen
# at test/ref/<name>_sh_ref.sh.
#
# Hermetic: PATH is reduced to a stub bin/, so "is dpkg installed", "does
# groupadd exist" and every install command are properties of the scenario, not
# of this machine. Each stub logs its argv; the comparison is that log, which is
# the only thing a module actually does to a box - what did it decide to run,
# with which arguments, in which order.
#
# The sh side runs with the real lib/*.sh (pkg.sh, service.sh, user.sh) around
# it; the C side runs the core. Both therefore go through their OWN package
# layer, which is exactly what is being compared: the C tier's native path
# (lib/module.c) against lib/pkg.sh's.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd); export OSR_DOTFILES
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
BIN="$TMP/bin"; mkdir -p "$BIN"
LOG="$TMP/log"; export LOG

# stub <name> [exit-code] — a fake tool that records how it was called.
stub() {
    rm -f "$BIN/$1"
    cat >"$BIN/$1" <<EOF
#!/bin/sh
printf '%s %s\n' "$1" "\$*" >>"\$LOG"
exit ${2:-0}
EOF
    chmod +x "$BIN/$1"
}
# The tools the sh libs themselves need to run at all.
for _t in sh env cat cut grep sed awk tr head tail printf id mktemp rm cp mkdir \
          tee sort od wc dirname basename sleep kill chmod test true false bash \
          find touch; do
    _p=$(command -v "$_t" 2>/dev/null) && ln -sf "$_p" "$BIN/$_t"
done

# The facts both sides read. OSR_LOG points into the sandbox so the step window
# never touches the real run log.
FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB OSR_DOTFILES=$OSR_DOTFILES
       OSR_PKG=apt OSR_INIT=systemd OSR_DISTRO=ubuntu OSR_ID_LIKE=debian
       OSR_CODENAME=noble OSR_VERSION_ID=24.04 OSR_ARCH=x86_64
       OSR_USER=tester OSR_HOME=$TMP/home OSR_THEME=nord NO_COLOR=1 TERM=dumb
       OSR_LOG=$TMP/run.log OSR_VERBOSE=1"

# sudo: the one tool both sides really do call. It records the escalation and
# then runs the command, so `as_root apt-get ...` in sh and osr_run_root() in C
# leave the same two lines.
# fc-list reports the Nerd Font already present, so the font step short-circuits
# on both tiers (§2). It is not what any of these scenarios is about, and a font
# install that cannot complete in a sandbox is only noise in the comparison.
cat >"$BIN/fc-list" <<'EOF'
#!/bin/sh
echo "/f: JetBrainsMono Nerd Font:style=Regular"
EOF
chmod +x "$BIN/fc-list"

cat >"$BIN/sudo" <<'EOF'
#!/bin/sh
printf 'sudo %s\n' "$*" >>"$LOG"
[ "$1" = "-u" ] && shift 2
exec "$@"
EOF
chmod +x "$BIN/sudo"

# One line, because the pty case below goes through a command STRING.
FACTS_1LINE=$(printf '%s' "$FACTS" | tr '\n' ' ')

# run_sh <module> — the frozen shell module, with the real libs around it.
run_sh() {
    : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" $FACTS HOME="$TMP/home" sh -c '
        . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
        # net and build come along because a package row may be a provider one
        # (source:provide_zig): the C tier always has those linked in, so the sh
        # side has to have them sourced, or the comparison is between a builder
        # and a "command not found".
        for l in detect user net pkg service build fonts gnome git; do
            . "$OSR_LIB/$l.sh"
        done
        . "$1"' _ "$OSR_ROOT/test/ref/$1_sh_ref.sh" >"$TMP/sh.out" 2>&1 || :
    cp "$LOG" "$TMP/sh.log"
}

# run_c <module> — the C module, through the core.
run_c() {
    : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" $FACTS HOME="$TMP/home" OSR_BIN="$OSR_BIN" \
        "$OSR_BIN" module run "$1" >"$TMP/c.out" 2>&1 || :
    cp "$LOG" "$TMP/c.log"
}

# normalize <file> — drop `id -u`, which is only how the SHELL as_root asks
# whether it is already root; the C tier calls getuid(). Same decision, one
# fewer process, and not a difference in what the module did to the box.
#
# The random temp suffix is collapsed for the same reason build_c_parity.sh
# collapses it: `mktemp -d` and mkdtemp() pick different numbers of characters,
# and a module that stages a download in one is not thereby a different module.
# The pid in a rendered template's name (osr-theme-<app>-<pid>-<file>,
# osr-wallpaper-layer-<pid>) goes the same way: it names one run, not one
# decision.
normalize() {
    grep -v '^id -u$' "$1" \
        | sed 's#tmp\.[A-Za-z0-9]*#tmp.X#g; s#\(osr-[a-z-]*-\)[0-9][0-9]*#\1PID#g' || :
}

# ..._env variants: the same two runners with extra facts (a sandboxed
# dotfiles tree and theme, for the config-layer cases).
run_sh_env() {
    : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" $FACTS $2 HOME="$TMP/home" sh -c '
        . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
        for l in detect user net pkg service config theme build fonts gnome git; do
            . "$OSR_LIB/$l.sh"
        done
        . "$1"' _ "$OSR_ROOT/test/ref/$1_sh_ref.sh" >"$TMP/sh.out" 2>&1 || :
    cp "$LOG" "$TMP/sh.log"
}
run_c_env() {
    : >"$LOG"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" $FACTS $2 HOME="$TMP/home" OSR_BIN="$OSR_BIN" \
        "$OSR_BIN" module run "$1" >"$TMP/c.out" 2>&1 || :
    cp "$LOG" "$TMP/c.log"
}

compare() {
    # An empty log on both sides would "agree" without either module having
    # done anything, which is exactly the bug this guard exists for.
    if [ ! -s "$TMP/sh.log" ]; then
        fail "$1 (the sh reference did nothing - the sandbox is wrong)"
        return
    fi
    if [ "$(normalize "$TMP/sh.log")" = "$(normalize "$TMP/c.log")" ]; then
        ok "$1"
    else
        fail "$1"
        diff -u "$TMP/sh.log" "$TMP/c.log" >&2 || :
    fi
}

# --- 1. flameshot: nothing installed yet -------------------------------------
stub dpkg 1            # nothing is installed
stub apt-get 0
run_sh flameshot; run_c flameshot
compare "flameshot: installs the four packages the same way"
assert_contains "$TMP/c.log" "apt-get install -y -q -o Dpkg::Use-Pty=0 flameshot maim slop xclip" \
    "flameshot: one batched apt-get, not four"

# --- 2. flameshot: already installed (the §2 rerun) --------------------------
stub dpkg 0            # everything is installed
run_sh flameshot; run_c flameshot
compare "flameshot: a rerun installs nothing"
refute_contains "$TMP/c.log" "apt-get install" "flameshot: no install on a rerun"

# --- 3. pkgmap resolution: a name that differs on this manager ---------------
# `fd = fd-find` in apt.map — the C tier must resolve it like _pkgmap_one did.
stub dpkg 1
_r=$(env -i PATH="$BIN" LOG="$LOG" $FACTS sh -c '. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
    . "$OSR_LIB/detect.sh"; . "$OSR_LIB/pkg.sh"; _pkgmap_one fd')
_c=$(env -i PATH="$BIN" LOG="$LOG" $FACTS "$OSR_BIN" module pkgmap fd 2>/dev/null || printf 'MISSING')
assert_eq "$_r" "$_c" "pkgmap: fd resolves the same on apt"
for _n in build dev-headers zsh btop nosuchpackage; do
    _r=$(env -i PATH="$BIN" LOG="$LOG" $FACTS sh -c '. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
        . "$OSR_LIB/detect.sh"; . "$OSR_LIB/pkg.sh"; _pkgmap_one "$1"' _ "$_n")
    _c=$(env -i PATH="$BIN" LOG="$LOG" $FACTS "$OSR_BIN" module pkgmap "$_n")
    assert_eq "$_r" "$_c" "pkgmap: $_n resolves the same"
done

# --- 3b. fastfetch: package + the themed config layer -----------------------
# The interesting half is the config: fastfetch ships ONE config.jsonc, so the
# theme's own file wins, else the dotfiles .tmpl is rendered with the theme's
# palette. The rendered bytes are compared, not just the commands.
mkdir -p "$TMP/home" "$TMP/df/fastfetch" "$TMP/themes/nord/config/fastfetch"
# On pacman, `fastfetch` is a bare passthrough; on apt/noble it is a `source:`
# provider row, which really downloads a .deb - not something a hermetic test
# can run. The provider path gets its own scenario at (d) below; these compare
# the config layering, which is the half that is new in C.
FF_ENV="OSR_DOTFILES=$TMP/df OSR_THEME=nord OSR_THEME_DIR=$TMP/themes/nord
        OSR_PKG=pacman OSR_CODENAME= OSR_VERSION_ID="
# pacman: not installed (`-Q` fails), installs fine (everything else). One
# blanket exit code would abort the run before the config layer, which is the
# half being compared here.
rm -f "$BIN/pacman"
cat >"$BIN/pacman" <<'EOF'
#!/bin/sh
printf 'pacman %s\n' "$*" >>"$LOG"
[ "$1" = "-Q" ] && exit 1
exit 0
EOF
chmod +x "$BIN/pacman"

# (a) the theme ships the file itself
printf 'THEME OWNED
' >"$TMP/themes/nord/config/fastfetch/config.jsonc"
printf 'DOTFILES BASE
' >"$TMP/df/fastfetch/config.jsonc"
stub dpkg 1; stub apt-get 0
run_sh_env fastfetch "$FF_ENV"; cp "$TMP/home/.config/fastfetch/config.jsonc" "$TMP/ff.sh" 2>/dev/null || :
rm -rf "$TMP/home"
run_c_env fastfetch "$FF_ENV"; cp "$TMP/home/.config/fastfetch/config.jsonc" "$TMP/ff.c" 2>/dev/null || :
compare "fastfetch: theme-owned config"
assert_eq "$(cat "$TMP/ff.sh" 2>/dev/null)" "$(cat "$TMP/ff.c" 2>/dev/null)" \
    "fastfetch: the installed file is the theme's own"

# (b) no theme file: the dotfiles template, rendered with the palette
rm -f "$TMP/themes/nord/config/fastfetch/config.jsonc"
cp "$OSR_ROOT/themes/nord/theme.list" "$TMP/themes/nord/theme.list"
printf 'bg={{background}} fg={{foreground_rgb}} dec={{accent_dec}} sgr={{accent_sgr}}
name={{THEME}} missing={{nosuchkey}} wall={{WALLPAPER_PATH}}
' \
    >"$TMP/df/fastfetch/config.jsonc.tmpl"
rm -rf "$TMP/home"
run_sh_env fastfetch "$FF_ENV"; cp "$TMP/home/.config/fastfetch/config.jsonc" "$TMP/ff.sh" 2>/dev/null || :
rm -rf "$TMP/home"
run_c_env fastfetch "$FF_ENV"; cp "$TMP/home/.config/fastfetch/config.jsonc" "$TMP/ff.c" 2>/dev/null || :
assert_eq "$(cat "$TMP/ff.sh" 2>/dev/null)" "$(cat "$TMP/ff.c" 2>/dev/null)" \
    "fastfetch: the rendered template is byte-identical"
assert_contains "$TMP/ff.c" "wall={{WALLPAPER_PATH}}" \
    "fastfetch: {{WALLPAPER_PATH}} is left for the second pass"
assert_contains "$TMP/ff.c" "missing={{nosuchkey}}" \
    "fastfetch: a key the theme lacks is left alone"
assert_eq "$(grep -c 'defines no nosuchkey' "$TMP/sh.out" 2>/dev/null)" \
    "$(grep -c 'defines no nosuchkey' "$TMP/c.out" 2>/dev/null)" \
    "fastfetch: both warn about the key the theme lacks"

# (c) neither: the dotfiles base file, unrendered
rm -f "$TMP/df/fastfetch/config.jsonc.tmpl"
rm -rf "$TMP/home"
run_sh_env fastfetch "$FF_ENV"; cp "$TMP/home/.config/fastfetch/config.jsonc" "$TMP/ff.sh" 2>/dev/null || :
rm -rf "$TMP/home"
run_c_env fastfetch "$FF_ENV"; cp "$TMP/home/.config/fastfetch/config.jsonc" "$TMP/ff.c" 2>/dev/null || :
assert_eq "DOTFILES BASE" "$(cat "$TMP/ff.c" 2>/dev/null)" "fastfetch: falls back to the dotfiles base"
assert_eq "$(cat "$TMP/ff.sh" 2>/dev/null)" "$(cat "$TMP/ff.c" 2>/dev/null)" "fastfetch: same fallback both ways"

# (d) the provider row: `fastfetch@noble = source:provide_fastfetch_deb` in
# apt.map. What is being checked here is the ROUTING - that a C module's
# pkg_install lands on the same builder the sh tier would have reached, and runs
# the same commands there. (The builders' own command sequences are compared one
# by one in test/unit/build_c_parity.sh; this is the wiring above them.)
#
# The sandbox needs a downloader for that: with none, the only thing the two
# tiers would differ on is how many times each retried a doomed `pkg_install
# curl` before giving up, which says nothing about the routing. So curl is
# stubbed the same way build_c_parity.sh stubs it - a fixed GitHub release, and
# an -o destination that gets created, since apt-get only runs when the .deb is
# actually there.
cat >"$BIN/curl" <<'EOF'
#!/bin/sh
printf 'curl %s\n' "$*" >>"$LOG"
_dest=""; _prev=""; _url=""
for _a in "$@"; do
    [ "$_prev" = "-o" ] && _dest=$_a
    case "$_a" in https://*) _url=$_a ;; esac
    _prev=$_a
done
case "$_url" in
    *api.github.com*/releases/latest) _json='{"tag_name": "v1.2.3"}' ;;
    *api.github.com*)                 _json='[{"name": "v1.2.3"}]' ;;
    *)                                _json="" ;;
esac
if [ -n "$_dest" ]; then
    printf 'payload\n' >"$_dest"
elif [ -n "$_json" ]; then
    printf '%s\n' "$_json"
fi
exit 0
EOF
chmod +x "$BIN/curl"
: >"$LOG"
stub dpkg 1
_prov=$(env -i PATH="$BIN" LOG="$LOG" $FACTS HOME="$TMP/home" \
    "$OSR_BIN" module pkgmap fastfetch)
assert_eq "source:provide_fastfetch_deb" "$_prov" "fastfetch: apt.map yields a provider row on noble"
rm -rf "$TMP/home"
run_sh_env fastfetch "OSR_DOTFILES=$TMP/df OSR_THEME=nord OSR_THEME_DIR=$TMP/themes/nord"
rm -rf "$TMP/home"
run_c_env fastfetch "OSR_DOTFILES=$TMP/df OSR_THEME=nord OSR_THEME_DIR=$TMP/themes/nord"
assert_contains "$TMP/sh.out" "building fastfetch from source" \
    "provider row: the sh tier reaches the builder"
assert_contains "$TMP/c.out" "building fastfetch from source" \
    "provider row: so does the C tier, through the same builder"
compare "provider row: identical command sequence either way"

# --- 3c. helpers: a C module with no sh predecessor -------------------------
# `helpers` is the first module written in C from scratch rather than ported, so
# there is nothing at test/ref to diff it against. What is checked instead is the
# behaviour that made it a module at all: the two seeded files, the identity each
# is written under, and that a rerun touches neither (§5 - seeded, then yours).
#
# The system file cannot be asserted directly: it lands under /usr/share, which
# this sandbox has no business writing. So the assertion is on the sudo stub's
# log - what the module DECIDED to run, which is the same thing every other
# scenario in this file compares.
rm -rf "$TMP/home"; mkdir -p "$TMP/home"
stub dpkg 1; stub apt-get 0
run_c helpers
assert_contains "$TMP/c.log" "apt-get install -y -q -o Dpkg::Use-Pty=0 exo-utils xterm" \
    "helpers: exo maps to exo-utils on apt, and xterm is the fallback terminal"
_hrc="$TMP/home/.config/xfce4/helpers.rc"
[ -f "$_hrc" ] && ok "helpers: helpers.rc seeded" || fail "helpers: helpers.rc seeded"
assert_contains "$_hrc" "TerminalEmulator=osr-term" \
    "helpers: the terminal role resolves to the session's own launcher"
assert_contains "$_hrc" "FileManager=Thunar" "helpers: the file-manager role too"
# The system helper entry is written as ROOT (it lands under /usr/share): the
# sudo stub is what proves the escalation happened, since the sandbox cannot own
# a real /usr/share write.
assert_contains "$TMP/c.log" "tee /usr/share/xfce4/helpers/osr-term.desktop" \
    "helpers: the osr-term helper entry is written under /usr/share"
assert_contains "$TMP/c.log" "sudo .*tee /usr/share/xfce4/helpers/osr-term.desktop" \
    "helpers: and it escalates to do it, unlike the file in \$HOME"
refute_contains "$TMP/c.log" "sudo -u tester tee /usr/share" \
    "helpers: the system file is not written as the riced user"

# A rerun must install nothing and rewrite nothing - the whole point of seeding.
printf 'TerminalEmulator=xterm\n' > "$_hrc"
stub dpkg 0
run_c helpers
refute_contains "$TMP/c.log" "apt-get install" "helpers: a rerun installs nothing"
assert_eq "TerminalEmulator=xterm" "$(cat "$_hrc")" \
    "helpers: a rerun leaves an edited helpers.rc alone"

# --- 3d. the package-only modules, one row each ------------------------------
# Two dozen modules whose whole body was `run_step "..." pkg_install <names>`.
# They are all the same shape, so they are one loop rather than two dozen
# scenarios: the frozen sh ref and the C module are run under the same stubs and
# their command logs diffed, which for a module of this shape IS the module.
#
# A name that resolves to a provider row on this target (amnezia-vpn, zig) is
# not skipped: both tiers then run the same builder, and that they agree there
# too is the point of not special-casing them.
rm -rf "$TMP/home"; mkdir -p "$TMP/home"
stub dpkg 1; stub apt-get 0
for _m in amnezia-vpn arandr brightnessctl celluloid discord feh gh git-base go \
          helvum htop hyprpicker inxi kdeconnect loupe luminance nautilus ncdu \
          nwg-displays onlyoffice qpwgraph vscode zig zip \
          obs-studio qbittorrent openssh vscode-insiders \
          networkmanager easyeffects wayland archives blueman disks flatpak \
          steam gvfs thunar audio thumbnails dnscrypt polkit-agent \
          hyprcursor keyring codecs printer ufw avahi xdg power; do
    # $HOME is wiped between the two runs: a module that WRITES there (steam
    # appends to .bashrc) would otherwise find its own sh-side output already in
    # place and skip the write, which is a difference in the sandbox and not in
    # the module.
    rm -rf "$TMP/home"; mkdir -p "$TMP/home"; run_sh "$_m"
    rm -rf "$TMP/home"; mkdir -p "$TMP/home"; run_c  "$_m"
    compare "$_m: the C module installs what the sh one did"
done

# vlc, redshift and zen-browser read a theme or the dotfiles tree, so they need
# a sandboxed one on BOTH sides: the C tier derives the dotfiles root from
# OSR_ROOT where the sh tier reads $OSR_DOTFILES, and with neither set the two
# would be looking at different trees rather than doing different things.
mkdir -p "$TMP/df/redshift" "$TMP/themes/nord/config/vlc" \
         "$TMP/themes/nord/config/firefox"
printf 'BASE REDSHIFT\n' >"$TMP/df/redshift/redshift.conf"
printf 'THEME VLCRC\n'   >"$TMP/themes/nord/config/vlc/vlcrc"
printf 'THEME CHROME\n'  >"$TMP/themes/nord/config/firefox/userChrome.css"
SANDBOX_ENV="OSR_DOTFILES=$TMP/df OSR_THEME=nord OSR_THEME_DIR=$TMP/themes/nord"
for _m in vlc redshift zen-browser input; do
    rm -rf "$TMP/home"; mkdir -p "$TMP/home"; run_sh_env "$_m" "$SANDBOX_ENV"
    (cd "$TMP/home" && find . -type f | sort) >"$TMP/tree.sh" 2>/dev/null || : >"$TMP/tree.sh"
    rm -rf "$TMP/home"; mkdir -p "$TMP/home"; run_c_env  "$_m" "$SANDBOX_ENV"
    (cd "$TMP/home" && find . -type f | sort) >"$TMP/tree.c" 2>/dev/null || : >"$TMP/tree.c"
    compare "$_m: same package and the same layer, against the same tree"
    assert_eq "$(cat "$TMP/tree.sh")" "$(cat "$TMP/tree.c")" \
        "$_m: and the same files land in \$HOME"
done

# dunst layers the dotfiles base and the theme drop-in beside it; waybar and
# hyprpaper take the theme's own tree. All three only act with a theme, so they
# go through the _env runners with one.
mkdir -p "$TMP/df/dunst" "$TMP/themes/nord/config/waybar" "$TMP/themes/nord/config/hypr"
printf 'BASE DUNSTRC\n' >"$TMP/df/dunst/dunstrc"
printf 'WAYBAR CONFIG\n' >"$TMP/themes/nord/config/waybar/config.jsonc"
printf 'WAYBAR STYLE\n'  >"$TMP/themes/nord/config/waybar/style.css"
printf 'WAYBAR DDC\n'    >"$TMP/themes/nord/config/waybar/waybar-ddc-module.sh"
printf 'preload = {{WALLPAPER_PATH}}\n' >"$TMP/themes/nord/config/hypr/hyprpaper.conf"
mkdir -p "$TMP/df/rofi" "$TMP/themes/nord/config/rofi" \
         "$TMP/themes/nord/config/gtklock" "$TMP/themes/nord/config/sddm/glass-theme"
for _f in config.rasi launcher.rasi powermenu.rasi; do
    printf 'ROFI %s\n' "$_f" >"$TMP/df/rofi/$_f"
done
printf 'ROFI COLORS\n'   >"$TMP/themes/nord/config/rofi/colors.rasi"
printf 'GTKLOCK INI\n'   >"$TMP/themes/nord/config/gtklock/config.ini"
printf 'bg = {{WALLPAPER_PATH}}\n' >"$TMP/themes/nord/config/gtklock/style.css"
printf 'FACE\n'          >"$TMP/themes/nord/config/gtklock/.face"
printf 'SDDM MAIN\n'     >"$TMP/themes/nord/config/sddm/hyprland.main.conf"
printf 'SDDM THEME\n'    >"$TMP/themes/nord/config/sddm/theme.conf.user"
printf 'QML\n'           >"$TMP/themes/nord/config/sddm/glass-theme/Main.qml"
mkdir -p "$TMP/df/micro" "$TMP/df/btop" "$TMP/df/zathura" "$TMP/df/mpv" \
         "$TMP/df/proteus" "$TMP/themes/nord/config/micro" \
         "$TMP/themes/nord/config/btop" "$TMP/themes/nord/config/zathura" \
         "$TMP/themes/nord/config/mpv" "$TMP/themes/nord/config/fcitx5" \
         "$TMP/themes/nord/config/wofi"
printf '{"tabsize": 4}\n'  >"$TMP/df/micro/settings.json"
printf '{"colorscheme": "nord"}\n' >"$TMP/themes/nord/config/micro/settings.json"
printf 'MICRO THEME\n'     >"$TMP/themes/nord/config/micro/theme.micro"
printf 'BTOP CONF\n'       >"$TMP/df/btop/btop.conf"
printf 'BTOP THEME\n'      >"$TMP/themes/nord/config/btop/btop.theme"
printf 'ZATHURARC\n'       >"$TMP/df/zathura/zathurarc"
printf 'ZATHURA THEME\n'   >"$TMP/themes/nord/config/zathura/90-theme.rc"
printf 'MPV CONF\n'        >"$TMP/df/mpv/mpv.conf"
printf 'MPV THEME\n'       >"$TMP/themes/nord/config/mpv/90-theme.conf"
printf 'PROTEUS TOML\n'    >"$TMP/df/proteus/proteus.toml"
printf 'FCITX THEME\n'     >"$TMP/themes/nord/config/fcitx5/theme.conf"
printf 'FCITX UI\n'        >"$TMP/themes/nord/config/fcitx5/classicui.conf"
printf 'WOFI CONFIG\n'     >"$TMP/themes/nord/config/wofi/config"
printf 'WOFI STYLE\n'      >"$TMP/themes/nord/config/wofi/style.css"
mkdir -p "$TMP/df/kate" "$TMP/df/picom" "$TMP/df/polybar/scripts" \
         "$TMP/themes/nord/config/kde" "$TMP/themes/nord/config/konsole" \
         "$TMP/themes/nord/config/kate" "$TMP/themes/nord/config/picom" \
         "$TMP/themes/nord/config/polybar" "$TMP/df/xdg"
printf 'KATERC\n'         >"$TMP/df/kate/katerc"
printf 'KDE COLORS\n'     >"$TMP/themes/nord/config/kde/color-scheme.colors"
printf 'KONSOLE\n'        >"$TMP/themes/nord/config/konsole/osr.colorscheme"
printf 'KATE THEME\n'     >"$TMP/themes/nord/config/kate/osr.theme"
printf 'PICOM CONF\n'     >"$TMP/df/picom/picom.conf"
printf 'PICOM LAUNCH\n'   >"$TMP/df/picom/launch.sh"
printf 'PICOM THEME\n'    >"$TMP/themes/nord/config/picom/90-theme.conf"
printf 'PB CONFIG\n'      >"$TMP/df/polybar/config.ini"
printf 'PB MODULES\n'     >"$TMP/df/polybar/modules.ini"
printf 'PB LAUNCH\n'      >"$TMP/df/polybar/launch.sh"
printf 'PB SCRIPT\n'      >"$TMP/df/polybar/scripts/battery.sh"
printf 'PB COLORS\n'      >"$TMP/themes/nord/config/polybar/colors.ini"
printf 'MIMEAPPS\n'       >"$TMP/df/xdg/mimeapps.list"
mkdir -p "$TMP/df/serie" "$TMP/df/alacritty" "$TMP/df/input" \
         "$TMP/themes/nord/config/serie" "$TMP/themes/nord/config/alacritty" \
         "$TMP/themes/nord/config/hypr" "$TMP/themes/nord/config/qt6ct" \
         "$TMP/themes/nord/config/wayland-sessions"
printf 'SERIE BASE\n'     >"$TMP/df/serie/config.toml"
printf 'SERIE THEME\n'    >"$TMP/themes/nord/config/serie/config.toml"
printf '[window]\n'       >"$TMP/df/alacritty/alacritty.toml"
printf 'ALA THEME\n'      >"$TMP/themes/nord/config/alacritty/alacritty-theme.toml"
printf 'GESTURES\n'       >"$TMP/df/input/libinput-gestures.conf"
printf 'env = WALLPAPER_PATH,{{WALLPAPER_PATH}}\n' \
                           >"$TMP/themes/nord/config/hypr/hyprland.conf"
printf 'AUDIO\n'          >"$TMP/themes/nord/config/hypr/start-audio.sh"
printf 'QT6CT\n'          >"$TMP/themes/nord/config/qt6ct/qt6ct.conf"
printf 'SESSION\n'        >"$TMP/themes/nord/config/wayland-sessions/hyprland.desktop"
printf 'LAUNCH\n'         >"$TMP/themes/nord/config/wayland-sessions/start-hyprland.sh"
mkdir -p "$TMP/df/cargo"
printf 'SHIM\n'           >"$TMP/df/cargo/cargo-binstall-shim"
mkdir -p "$TMP/themes/nord/config/copyq" "$TMP/themes/nord/config/thunderbird" \
         "$TMP/themes/nord/config/betterlockscreen" "$TMP/df/thunderbird"
printf '[Theme]\ncolor_bg=#000\n' >"$TMP/themes/nord/config/copyq/theme.ini"
printf 'CHROME\n'         >"$TMP/themes/nord/config/thunderbird/userChrome.css"
printf 'USERJS\n'         >"$TMP/df/thunderbird/user.js"
printf 'BLSRC\n'          >"$TMP/themes/nord/config/betterlockscreen/betterlockscreenrc"
THEME_ENV="OSR_DOTFILES=$TMP/df OSR_THEME=nord OSR_THEME_DIR=$TMP/themes/nord"
for _m in dunst waybar hyprpaper rofi gtklock sddm micro btop viewers \
          proteus fcitx5 wofi kate picom polybar serie alacritty hyprland \
          copyq thunderbird i3lock; do
    rm -rf "$TMP/home"; mkdir -p "$TMP/home"
    run_sh_env "$_m" "$THEME_ENV"
    (cd "$TMP/home" && find . -type f | sort) >"$TMP/tree.sh" 2>/dev/null || : >"$TMP/tree.sh"
    rm -rf "$TMP/home"; mkdir -p "$TMP/home"
    run_c_env "$_m" "$THEME_ENV"
    (cd "$TMP/home" && find . -type f | sort) >"$TMP/tree.c" 2>/dev/null || : >"$TMP/tree.c"
    compare "$_m: same packages and the same config layering"
    assert_eq "$(cat "$TMP/tree.sh")" "$(cat "$TMP/tree.c")" \
        "$_m: and the same files land in \$HOME"
done
rm -rf "$TMP/home"; mkdir -p "$TMP/home"; run_c_env waybar "$THEME_ENV"
assert_contains "$TMP/home/.config/waybar/config.jsonc" "WAYBAR CONFIG" \
    "waybar: the layout comes from the theme's own tree"

# The lock screens and mako are the same shape plus one file: the theme owns
# the whole config, and a theme that ships none leaves the package default. Both
# halves are checked - the commands, and the file that ended up in $HOME.
mkdir -p "$TMP/themes/nord/config/hypr" "$TMP/themes/nord/config/waylock" \
         "$TMP/themes/nord/config/swaylock" "$TMP/themes/nord/config/mako"
printf 'THEME LOCK\n' >"$TMP/themes/nord/config/hypr/hypridle.conf"
printf 'THEME LOCK\n' >"$TMP/themes/nord/config/hypr/hyprlock.conf"
printf 'THEME LOCK\n' >"$TMP/themes/nord/config/waylock/waylock.toml"
printf 'THEME LOCK\n' >"$TMP/themes/nord/config/swaylock/config"
printf 'THEME LOCK\n' >"$TMP/themes/nord/config/mako/config"
LOCK_ENV="OSR_DOTFILES=$TMP/df OSR_THEME=nord OSR_THEME_DIR=$TMP/themes/nord"
for _m in hypridle:hypr/hypridle.conf hyprlock:hypr/hyprlock.conf \
          waylock:waylock/waylock.toml swaylock:swaylock/config mako:mako/config; do
    _name=${_m%%:*}; _rel=${_m#*:}
    rm -rf "$TMP/home"; mkdir -p "$TMP/home"
    run_sh_env "$_name" "$LOCK_ENV"
    cp "$TMP/home/.config/$_rel" "$TMP/lock.sh" 2>/dev/null || rm -f "$TMP/lock.sh"
    rm -rf "$TMP/home"; mkdir -p "$TMP/home"
    run_c_env "$_name" "$LOCK_ENV"
    cp "$TMP/home/.config/$_rel" "$TMP/lock.c" 2>/dev/null || rm -f "$TMP/lock.c"
    compare "$_name: same package and same theme layer"
    assert_eq "$(cat "$TMP/lock.sh" 2>/dev/null)" "$(cat "$TMP/lock.c" 2>/dev/null)" \
        "$_name: the installed file is the theme's"
    assert_eq "THEME LOCK" "$(cat "$TMP/lock.c" 2>/dev/null)" \
        "$_name: and it is the theme's own bytes"
done

# No theme: the package default is left alone, and neither tier writes anything.
rm -rf "$TMP/home"; mkdir -p "$TMP/home"
run_sh hyprlock; run_c hyprlock
compare "hyprlock: with no theme, only the package is installed"
if [ -f "$TMP/home/.config/hypr/hyprlock.conf" ]; then
    fail "hyprlock: and nothing is written into \$HOME"
else
    ok "hyprlock: and nothing is written into \$HOME"
fi

# The Arch-only swaps and helpers take an early exit on an apt target, and say
# so. Same shape as paru below, checked as a group.
for _m in dkms pacman-multilib pulseaudio pipewire vmware-init; do
    run_sh "$_m"; run_c "$_m"
    if [ ! -s "$TMP/sh.log" ] && [ ! -s "$TMP/c.log" ]; then
        ok "$_m: an Arch-only module does nothing on apt, both tiers"
    else
        fail "$_m: an Arch-only module does nothing on apt, both tiers"
        diff -u "$TMP/sh.log" "$TMP/c.log" >&2 || :
    fi
done
assert_eq "$(cat "$TMP/sh.out")" "$(cat "$TMP/c.out")" \
    "vmware-init: and the two tiers give the same reason"

# paru is Arch-only and this target is apt; cpu-microcodes has no vendor to act
# on unless osr_detect found one; gpaste stops at "no gnome-shell here", which is
# the first thing it checks. All three therefore run NOTHING, which is the
# assertion - and the one thing compare() cannot make, since it reads an empty
# sh log as a broken sandbox.
for _m in paru cpu-microcodes gpaste; do
    run_sh "$_m"; run_c "$_m"
    if [ ! -s "$TMP/sh.log" ] && [ ! -s "$TMP/c.log" ]; then
        ok "$_m: does nothing on a target it has nothing to do on, both tiers"
    else
        fail "$_m: does nothing on a target it has nothing to do on, both tiers"
        diff -u "$TMP/sh.log" "$TMP/c.log" >&2 || :
    fi
done
# Each of the three says why it did nothing, and the message is the only output
# there is - so it is checked while that module's run is still the last one.
run_c cpu-microcodes
assert_contains "$TMP/c.out" "no microcode installed" \
    "cpu-microcodes: and says why it installed none"
run_c gpaste
assert_contains "$TMP/c.out" "gnome-shell not found" \
    "gpaste: and says it needs a GNOME Shell to be a clipboard manager for"

# Given a vendor, it installs that vendor's microcode and only that one.
run_sh_env cpu-microcodes "OSR_CPU_VENDOR=AuthenticAMD"
run_c_env  cpu-microcodes "OSR_CPU_VENDOR=AuthenticAMD"
compare "cpu-microcodes: AMD gets amd-ucode, both tiers"
refute_contains "$TMP/c.log" "intel-ucode" "cpu-microcodes: and not the Intel one"

# A rerun of any of them installs nothing - the §2 contract, checked once on a
# module whose row is a plain native package.
stub dpkg 0
run_sh htop; run_c htop
compare "htop: a rerun installs nothing, both tiers"
refute_contains "$TMP/c.log" "apt-get install" "htop: and no install command is run"
stub dpkg 1

# --- 4. docker: the full path (package, group, membership, service) ----------
stub dpkg 1
stub getent 1          # no docker group yet
stub groupadd 0
stub usermod 0
stub id 0
stub systemctl 1       # not enabled yet -> enable --now
run_sh docker; run_c docker
compare "docker: package + group + membership + service"
assert_contains "$TMP/c.log" "groupadd docker" "docker: creates the group"
assert_contains "$TMP/c.log" "usermod -aG docker tester" "docker: adds the user"
assert_contains "$TMP/c.log" "systemctl enable --now docker" "docker: enables the daemon"

# --- 5. docker on a busybox box: addgroup instead of groupadd ----------------
rm -f "$BIN/groupadd" "$BIN/usermod" "$BIN/getent"
stub addgroup 0
run_sh docker; run_c docker
compare "docker: busybox tooling (addgroup)"

# --- 6. docker, already done: the §2 rerun ----------------------------------
stub dpkg 0
stub getent 0          # group exists
stub systemctl 0       # enabled AND active
rm -f "$BIN/addgroup"
stub usermod 0
stub groupadd 0
# `id -nG tester` says the user is already in it
cat >"$BIN/id" <<'EOF'
#!/bin/sh
printf 'id %s\n' "$*" >>"$LOG"
[ "$1" = "-nG" ] && { echo "tester docker sudo"; exit 0; }
echo 1000
EOF
chmod +x "$BIN/id"
run_sh docker; run_c docker
compare "docker: a fully-applied box changes nothing"
refute_contains "$TMP/c.log" "groupadd" "docker rerun: no group creation"
refute_contains "$TMP/c.log" "usermod" "docker rerun: no membership change"

# --- 7. the live step window, on a real terminal -----------------------------
# Everything above runs with OSR_VERBOSE set, which takes the plain streamed
# path - so none of it exercises the window that repaints while a step runs,
# and none of it could catch the bug this section exists for: the spin loop
# must REAP its own child. An exited child stays a pid until someone waits on
# it, so a loop that asks "does this pid still exist" (which is what the shell
# version could do, because the shell reaps in the background) never ends.
if command -v script >/dev/null 2>&1 && command -v timeout >/dev/null 2>&1; then
    # A package manager that takes a moment and prints as it goes, so the
    # window actually paints more than one frame.
    rm -f "$BIN/pacman"
    cat >"$BIN/pacman" <<'EOF'
#!/bin/sh
printf 'pacman %s
' "$*" >>"$LOG"
[ "$1" = "-Q" ] && exit 1
i=1
while [ "$i" -le 4 ]; do echo "installing chunk $i"; sleep 0.15; i=$((i + 1)); done
exit 0
EOF
    chmod +x "$BIN/pacman"
    rm -f "$BIN/sudo"
    printf '#!/bin/sh
[ "$1" = "-u" ] && shift 2
exec "$@"
' >"$BIN/sudo"
    chmod +x "$BIN/sudo"
    rm -rf "$TMP/home"; mkdir -p "$TMP/home"
    # One line: this goes through `script -c`, so a newline would end the
    # command and leave the rest of the environment unset.
    _live="env -i PATH=$BIN LOG=$LOG HOME=$TMP/home OSR_BIN=$OSR_BIN $FACTS_1LINE OSR_PKG=pacman OSR_CODENAME= OSR_VERSION_ID= OSR_VERBOSE= TERM=xterm $OSR_BIN module run flameshot"
    _rc=0
    timeout 25 script -q -c "$_live" /dev/null >"$TMP/live.out" 2>&1 || _rc=$?
    if [ "$_rc" -eq 124 ]; then
        fail "live window: the step never finished (the spinner hung)"
    else
        ok "live window: the step finished"
    fi
    # Four spaces, not one: the tag pads out to OSR_TAG_WIDTH so the text
    # lands in the same column as the [INFO] lines around it.
    assert_contains "$TMP/live.out" '\[ok\]    Installing screenshot tools' \
        "live window: collapses to one [ok] line"
    assert_contains "$TMP/live.out" 'installing chunk' \
        "live window: showed the command's output while it ran"
else
    ok "live window checks skipped (no script(1)/timeout(1))"
fi

finish
