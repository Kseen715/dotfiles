#!/bin/sh
# Proves lib/config.c lays config down exactly as lib/config.sh did.
#
# Unlike the pkg/build parity tests, the contract here is not a list of commands
# but the BYTES THAT LAND: a seeded 00-env, an owned block inside a file the user
# also writes to, a composed starship.toml, a palette downgraded for an old foot.
# So each scenario runs the sh function and the C subcommand against two
# identical sandboxes and compares the resulting trees byte for byte, plus the
# messages each printed.
#
# The sandbox is hermetic in the same way: PATH is reduced to a stub bin/, and
# the version-reporting tools (foot, alacritty) are stubs whose answer is the
# scenario's input. OSR_USER is the real user, so as_user runs in place and the
# files actually appear.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip config_c_parity: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
BIN="$TMP/bin"; mkdir -p "$BIN"

for _t in sh env cat cut grep sed awk tr head tail printf id mktemp rm cp mv mkdir \
          rmdir touch ln find sort wc cmp dirname basename python3 test true false; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in
        /*) ln -sf "$_p" "$BIN/$_t" ;;
    esac
done

# tee is how lib/module.c writes a root-owned file; nothing here needs it, but a
# missing tee would fail differently on the two sides, which would hide a bug.
_p=$(command -v tee 2>/dev/null) && ln -sf "$_p" "$BIN/tee"

# ver_stub <name> <line> -- a tool that answers --version and nothing else.
ver_stub() {
    cat >"$BIN/$1" <<EOF
#!/bin/sh
[ "\$1" = "--version" ] && printf '%s\n' "$2"
exit 0
EOF
    chmod +x "$BIN/$1"
}

FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB OSR_BIN=$OSR_BIN
       OSR_DISTRO=ubuntu OSR_ID_LIKE=debian OSR_ARCH=x86_64 OSR_ARCH_DEB=amd64
       OSR_USER=$(id -un) NO_COLOR=1 TERM=dumb COLUMNS=80 OSR_VERBOSE=1"

# The scenario writes its input files into $ROOT. It is called once per side,
# with the two sides' roots, so both start from identical trees.
seed() { :; }

# run_side <root> <sh|c> <command> -- <command> is eval'd with $ROOT bound to
# that side's sandbox: the sh side calls the shell function, the C side calls
# `osr config ...` with the same arguments.
run_side() {
    _r=$1; _side=$2; _cmd=$3
    rm -rf "$_r"; mkdir -p "$_r/home" "$_r/tmp" "$_r/theme"
    ROOT=$_r seed "$_r"
    if [ "$_side" = sh ]; then
        # shellcheck disable=SC2086
        env -i PATH="$BIN" $FACTS $EXTRA ROOT="$_r" HOME="$_r/home" \
            OSR_HOME="$_r/home" OSR_THEME_DIR="$_r/theme" TMPDIR="$_r/tmp" \
            OSR_LOG="$_r/run.log" sh -c '
                . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
                for l in detect user config; do . "$OSR_LIB/$l.sh"; done
                eval "$1"' _ "$_cmd" >"$_r/out" 2>&1 || printf 'EXIT %s\n' "$?" >>"$_r/out"
    else
        # shellcheck disable=SC2086
        env -i PATH="$BIN" $FACTS $EXTRA ROOT="$_r" HOME="$_r/home" \
            OSR_HOME="$_r/home" OSR_THEME_DIR="$_r/theme" TMPDIR="$_r/tmp" \
            OSR_LOG="$_r/run.log" sh -c '
                eval "\"$OSR_BIN\" config $1"' _ "$_cmd" >"$_r/out" 2>&1 || printf 'EXIT %s\n' "$?" >>"$_r/out"
    fi
}

# dump_tree <root> -- every path under the sandbox and every byte in it, with
# the sandbox path itself collapsed to ROOT. A loader block names the directory
# it loads from, so the two sides' files are only comparable once their own
# roots are spelled the same.
dump_tree() {
    (cd "$1" && find . | sort | while read -r _f; do
        if [ -d "$_f" ]; then
            printf 'dir  %s\n' "$_f"
        else
            printf 'file %s\n' "$_f"
            sed 's#^#    #' "$_f"
        fi
    done) | sed "s#$1#ROOT#g"
}

# scene <label> <sh-command> <c-command> -- run the pair and compare what landed
# and what was said. Sandbox paths are collapsed to ROOT so the two outputs are
# comparable at all; the temp dir each side used is its own business.
scene() {
    _label=$1; _sh=$2; _c=$3
    run_side "$TMP/sh" sh "$_sh"
    run_side "$TMP/c"  c  "$_c"
    rm -rf "$TMP/sh/tmp" "$TMP/c/tmp"
    rm -f "$TMP/sh/run.log" "$TMP/c/run.log"
    sed "s#$TMP/sh#ROOT#g" "$TMP/sh/out" >"$TMP/sh.out"
    sed "s#$TMP/c#ROOT#g"  "$TMP/c/out"  >"$TMP/c.out"
    rm -f "$TMP/sh/out" "$TMP/c/out"
    dump_tree "$TMP/sh" >"$TMP/sh.tree"
    dump_tree "$TMP/c"  >"$TMP/c.tree"
    if diff -u "$TMP/sh.tree" "$TMP/c.tree" >"$TMP/tree.diff" 2>&1; then
        ok "$_label: the same files, byte for byte"
    else
        fail "$_label: the trees differ"
        cat "$TMP/tree.diff" >&2
    fi
    if diff -u "$TMP/sh.out" "$TMP/c.out" >"$TMP/out.diff" 2>&1; then
        ok "$_label: the same messages"
    else
        fail "$_label: the messages differ"
        cat "$TMP/out.diff" >&2
    fi
}

EXTRA=""

# --- 1. seeded layers (00-env, 99-local) -------------------------------------
seed() { printf 'export A=1\n' >"$1/src"; }
scene "seed_once into an absent dotfile" \
      'seed_once "$ROOT/src" "$ROOT/home/.config/zsh/rc.d/00-env.zsh"' \
      'seed-once "$ROOT/src" "$ROOT/home/.config/zsh/rc.d/00-env.zsh"'
assert_contains "$TMP/c/home/.config/zsh/rc.d/00-env.zsh" 'export A=1' \
    "seed_once: the seed actually landed"

seed() {
    printf 'export A=1\n' >"$1/src"
    mkdir -p "$1/home/.config/zsh/rc.d"
    printf 'MINE\n' >"$1/home/.config/zsh/rc.d/00-env.zsh"
}
scene "seed_once keeps what the user already has" \
      'seed_once "$ROOT/src" "$ROOT/home/.config/zsh/rc.d/00-env.zsh"' \
      'seed-once "$ROOT/src" "$ROOT/home/.config/zsh/rc.d/00-env.zsh"'
assert_contains "$TMP/c/home/.config/zsh/rc.d/00-env.zsh" 'MINE' \
    "seed_once: the user's own file is untouched"
assert_contains "$TMP/c.out" 'seeded once' \
    "seed_once: and it says why it did nothing"

seed() { :; }
scene "seed_empty creates the 99-local layer" \
      'seed_empty "$ROOT/home/.config/zsh/rc.d/99-local.zsh"' \
      'seed-empty "$ROOT/home/.config/zsh/rc.d/99-local.zsh"'

seed() {
    mkdir -p "$1/home/.config/zsh/rc.d"
    printf 'local stuff\n' >"$1/home/.config/zsh/rc.d/99-local.zsh"
}
scene "seed_empty never truncates an existing 99-local" \
      'seed_empty "$ROOT/home/.config/zsh/rc.d/99-local.zsh"' \
      'seed-empty "$ROOT/home/.config/zsh/rc.d/99-local.zsh"'

# --- 2. owned blocks ----------------------------------------------------------
seed() { :; }
scene "install_zsh_loader writes the block into a fresh .zshrc" \
      'install_zsh_loader "$ROOT/home/.config/zsh/rc.d" "$ROOT/home/.zshrc"' \
      'zsh-loader "$ROOT/home/.config/zsh/rc.d" "$ROOT/home/.zshrc"'
assert_contains "$TMP/c/home/.zshrc" '# >>> os-rice:loader >>>' \
    "install_zsh_loader: the block is marked"
assert_contains "$TMP/c/home/.zshrc" 'for _f in .*rc\.d"/\*\.zsh' \
    "install_zsh_loader: it sources the drop-in dir in lexical order"

seed() {
    { printf 'BEFORE\n'
      printf '# >>> os-rice:loader >>>\nold loader\n# <<< os-rice:loader <<<\n'
      printf 'AFTER\n'; } >"$1/home/.zshrc"
}
scene "install_zsh_loader rewrites only its own block" \
      'install_zsh_loader "$ROOT/home/.config/zsh/rc.d" "$ROOT/home/.zshrc"' \
      'zsh-loader "$ROOT/home/.config/zsh/rc.d" "$ROOT/home/.zshrc"'
assert_contains "$TMP/c/home/.zshrc" '^BEFORE$' \
    "install_zsh_loader: the user's lines above survive"
assert_contains "$TMP/c/home/.zshrc" '^AFTER$' \
    "install_zsh_loader: and the ones below"
refute_contains "$TMP/c/home/.zshrc" 'old loader' \
    "install_zsh_loader: the previous block body is gone"

seed() { printf 'user zshenv\n' >"$1/home/.zshenv"; }
scene "install_zsh_zshenv appends its block" \
      'install_zsh_zshenv "$ROOT/home/.zshenv"' \
      'zsh-zshenv "$ROOT/home/.zshenv"'
assert_contains "$TMP/c/home/.zshenv" 'skip_global_compinit=1' \
    "install_zsh_zshenv: the compinit opt-out is what it writes"

seed() { :; }
scene "install_xprofile_loader writes the .sh loader" \
      'install_xprofile_loader "$ROOT/home/.config/xprofile.d" "$ROOT/home/.xprofile"' \
      'xprofile-loader "$ROOT/home/.config/xprofile.d" "$ROOT/home/.xprofile"'
assert_contains "$TMP/c/home/.xprofile" 'xprofile-loader' \
    "install_xprofile_loader: its block has its own name"

# --- 3. composed configs ------------------------------------------------------
seed() {
    printf '{\n  "editor.fontSize": 13,\n  "workbench.colorTheme": "Default"\n}\n' >"$1/base.json"
    printf '{\n  "workbench.colorTheme": "Nord"\n}\n' >"$1/frag.json"
}
scene "compose_json_config merges the rice keys over the base" \
      'compose_json_config "$ROOT/base.json" "$ROOT/frag.json" "$ROOT/home/settings.json"' \
      'json "$ROOT/base.json" "$ROOT/frag.json" "$ROOT/home/settings.json"'
assert_contains "$TMP/c/home/settings.json" '"workbench.colorTheme": "Nord"' \
    "compose_json_config: the rice key won"
assert_contains "$TMP/c/home/settings.json" '"editor.fontSize": 13' \
    "compose_json_config: and the base key stayed"

seed() { printf '{\n  "editor.fontSize": 13\n}\n' >"$1/base.json"; }
scene "compose_json_config installs the base when the rice has no fragment" \
      'compose_json_config "$ROOT/base.json" "$ROOT/missing.json" "$ROOT/home/settings.json"' \
      'json "$ROOT/base.json" "$ROOT/missing.json" "$ROOT/home/settings.json"'
assert_contains "$TMP/c.out" 'installing the base' \
    "compose_json_config: a missing fragment is reported, not fatal"

seed() {
    printf 'format = "$all"\n\n[palettes.theme]\nred = "#000000"\n' >"$1/base.toml"
    printf '[palettes.theme]\nred = "#bf616a"\n' >"$1/palette.toml"
}
scene "compose_starship_config swaps the palette table" \
      'compose_starship_config "$ROOT/base.toml" "$ROOT/palette.toml" "$ROOT/home/starship.toml"' \
      'starship "$ROOT/base.toml" "$ROOT/palette.toml" "$ROOT/home/starship.toml"'
assert_contains "$TMP/c/home/starship.toml" '#bf616a' \
    "compose_starship_config: the rice palette is in"
refute_contains "$TMP/c/home/starship.toml" '#000000' \
    "compose_starship_config: and the base's default palette is out"
assert_contains "$TMP/c/home/starship.toml" 'format = "\$all"' \
    "compose_starship_config: the base body above the table survives"

# --- 4. configs adapted to the installed app ----------------------------------
seed() { printf '[colors-dark]\nbackground=2e3440\n\n[colors-light]\nbackground=eceff4\n' >"$1/palette.ini"; }

ver_stub foot 'foot version: 1.26.1 -pgo'
scene "install_foot_palette leaves the new section names alone on foot 1.26" \
      'install_foot_palette "$ROOT/palette.ini" "$ROOT/home/colors.ini"' \
      'foot-palette "$ROOT/palette.ini" "$ROOT/home/colors.ini"'
assert_contains "$TMP/c/home/colors.ini" '^\[colors-dark\]$' \
    "install_foot_palette: a new foot gets [colors-dark] verbatim"

ver_stub foot 'foot version: 1.20.2'
scene "install_foot_palette downgrades the section names for an old foot" \
      'install_foot_palette "$ROOT/palette.ini" "$ROOT/home/colors.ini"' \
      'foot-palette "$ROOT/palette.ini" "$ROOT/home/colors.ini"'
assert_contains "$TMP/c/home/colors.ini" '^\[colors\]$' \
    "install_foot_palette: [colors-dark] became [colors]"
assert_contains "$TMP/c/home/colors.ini" '^\[colors2\]$' \
    "install_foot_palette: [colors-light] became [colors2]"

rm -f "$BIN/foot"
scene "install_foot_palette assumes the old names when foot is not installed" \
      'install_foot_palette "$ROOT/palette.ini" "$ROOT/home/colors.ini"' \
      'foot-palette "$ROOT/palette.ini" "$ROOT/home/colors.ini"'
assert_contains "$TMP/c/home/colors.ini" '^\[colors\]$' \
    "install_foot_palette: no foot means the name every version accepts"

seed() { printf '[general]\nimport = ["theme.toml"]\n\n[font]\nsize = 11\n' >"$1/alacritty.toml"; }

ver_stub alacritty 'alacritty 0.15.1 (abcdef)'
scene "install_alacritty_config keeps [general] on 0.15" \
      'install_alacritty_config "$ROOT/alacritty.toml" "$ROOT/home/alacritty.toml"' \
      'alacritty "$ROOT/alacritty.toml" "$ROOT/home/alacritty.toml"'
assert_contains "$TMP/c/home/alacritty.toml" '^\[general\]$' \
    "install_alacritty_config: 0.14+ understands the section"

ver_stub alacritty 'alacritty 0.13.2 (abcdef)'
scene "install_alacritty_config drops [general] on 0.13" \
      'install_alacritty_config "$ROOT/alacritty.toml" "$ROOT/home/alacritty.toml"' \
      'alacritty "$ROOT/alacritty.toml" "$ROOT/home/alacritty.toml"'
refute_contains "$TMP/c/home/alacritty.toml" '^\[general\]$' \
    "install_alacritty_config: 0.13 would reject the section"
assert_contains "$TMP/c/home/alacritty.toml" 'import = \["theme.toml"\]' \
    "install_alacritty_config: import survives as a top-level key"

ver_stub alacritty 'alacritty 0.12.3 (abcdef)'
scene "install_alacritty_config warns on the YAML-era 0.12" \
      'install_alacritty_config "$ROOT/alacritty.toml" "$ROOT/home/alacritty.toml"' \
      'alacritty "$ROOT/alacritty.toml" "$ROOT/home/alacritty.toml"'
assert_contains "$TMP/c.out" 'predates the TOML config' \
    "install_alacritty_config: below 0.13 it says the file will be ignored"

rm -f "$BIN/alacritty"
scene "install_alacritty_config assumes current when alacritty is absent" \
      'install_alacritty_config "$ROOT/alacritty.toml" "$ROOT/home/alacritty.toml"' \
      'alacritty "$ROOT/alacritty.toml" "$ROOT/home/alacritty.toml"'
assert_contains "$TMP/c/home/alacritty.toml" '^\[general\]$' \
    "install_alacritty_config: an unknown version keeps the modern shape"

# --- 5. whole theme-owned directories -----------------------------------------
seed() {
    mkdir -p "$1/theme/config/xfce4/xfconf"
    printf 'a\n' >"$1/theme/config/xfce4/xfconf/x.xml"
}
scene "apply_config copies the theme's config dir into ~/.config" \
      'apply_config xfce4' 'apply xfce4'
assert_contains "$TMP/c/home/.config/xfce4/xfconf/x.xml" 'a' \
    "apply_config: the contents land, not the directory nested in itself"

seed() { :; }
scene "apply_config skips a config the theme does not ship" \
      'apply_config nothing' 'apply nothing'
assert_contains "$TMP/c.out" "not found in theme" \
    "apply_config: a missing dir is a warning, not a failure"

# --- 6. Mozilla profiles ------------------------------------------------------
seed() {
    mkdir -p "$1/moz/abc.default-release" "$1/moz/xyz.dev-edition-default"
    printf '[Profile0]\nName=default\nIsRelative=1\nPath=abc.default-release\n' \
        >"$1/moz/profiles.ini"
}
scene "osr_mozilla_profiles reads profiles.ini" \
      'osr_mozilla_profiles "$ROOT/moz"' 'mozilla-profiles "$ROOT/moz"'
assert_contains "$TMP/c.out" 'moz/abc.default-release$' \
    "osr_mozilla_profiles: a relative Path= is resolved against the root"
refute_contains "$TMP/c.out" 'dev-edition' \
    "osr_mozilla_profiles: profiles.ini wins over the glob when it exists"

seed() {
    mkdir -p "$1/moz/abc.default-release" "$1/moz/xyz.dev-edition-default"
}
scene "osr_mozilla_profiles falls back to the glob without profiles.ini" \
      'osr_mozilla_profiles "$ROOT/moz"' 'mozilla-profiles "$ROOT/moz"'
assert_contains "$TMP/c.out" 'dev-edition' \
    "osr_mozilla_profiles: the dev edition is found too"

seed() {
    mkdir -p "$1/moz/abc.default-release"
    printf 'user_pref("x", 1);\n' >"$1/user.js"
    printf '* { color: red }\n' >"$1/userChrome.css"
}
scene "install_mozilla_layer installs prefs and chrome into every profile" \
      'install_mozilla_layer "$ROOT/moz" "$ROOT/user.js" "$ROOT/userChrome.css"' \
      'mozilla "$ROOT/moz" "$ROOT/user.js" "$ROOT/userChrome.css"'
assert_contains "$TMP/c/moz/abc.default-release/user.js" 'user_pref' \
    "install_mozilla_layer: user.js lands in the profile"
assert_contains "$TMP/c/moz/abc.default-release/chrome/userChrome.css" 'color: red' \
    "install_mozilla_layer: userChrome.css lands under chrome/"

seed() { mkdir -p "$1/moz"; printf 'user_pref("x", 1);\n' >"$1/user.js"; }
scene "install_mozilla_layer warns when the app has no profile yet" \
      'install_mozilla_layer "$ROOT/moz" "$ROOT/user.js" ""' \
      'mozilla "$ROOT/moz" "$ROOT/user.js" ""'
assert_contains "$TMP/c.out" 'launch the app once' \
    "install_mozilla_layer: a profile-less app is a warning, not a failure"

finish
