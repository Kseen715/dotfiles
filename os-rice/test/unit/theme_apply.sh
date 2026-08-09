#!/bin/sh
# Proves the theme-only apply path (§6a, lib/apply.sh) - the one bound to a
# hotkey, so its contract is as much about what it does NOT do:
#
#   1. every mutating verb in the install/build/download/service libs is
#      neutralized, derived from the sources so a new provider is inert for free
#   2. read-only queries survive (modules branch on them)
#   3. the layer set is the installed rice's modules, not all 113
#   4. a real end-to-end apply rewrites the 90-* layers and touches no package
#   5. re-applying is idempotent, and switching back and forth is symmetric
#   6. as_root is skipped without a sudo ticket instead of blocking on a prompt
#
# Hermetic: temp HOME, no net, no root, no package manager.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd); export OSR_DOTFILES
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

# --- 1. the neutralized set is derived from the libs --------------------------
(
    . "$OSR_LIB/detect.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/pkg.sh"; . "$OSR_LIB/net.sh"
    . "$OSR_LIB/git.sh"; . "$OSR_LIB/service.sh"; . "$OSR_LIB/fonts.sh"; . "$OSR_LIB/build.sh"
    . "$OSR_LIB/config.sh"; . "$OSR_LIB/theme.sh"; . "$OSR_LIB/apply.sh"
    osr_detect >/dev/null 2>&1   # _pkgmap_one reads $OSR_PKG to pick its table

    _verbs=$(_osr_apply_verbs)
    [ -n "$_verbs" ] || fail "_osr_apply_verbs found no functions to stub"

    # The verbs a module actually calls must all be in that derived set, or the
    # apply path would run them for real.
    for v in pkg_install pkg_refresh pkg_remove enable_service osr_install_nerd_font \
             install_omz install_zsh_plugin osr_download provide_chafa provide_ghostty; do
        case " $(printf '%s' "$_verbs" | tr '\n' ' ') " in
            *" $v "*) ;;
            *) fail "$v is not in the derived stub set" ;;
        esac
    done
    ok "every install/build/download/service verb a module calls is in the stub set"

    # Marker files prove the stub replaced the real function rather than the
    # real function merely being absent.
    MARK=$(mktemp -d)
    export MARK
    osr_apply_stub_mutators

    pkg_install curl && ok "pkg_install returns success as a no-op" \
        || fail "pkg_install stub should succeed"
    [ ! -f "$MARK/installed" ] && ok "pkg_install installed nothing" \
        || fail "pkg_install installed something"

    # 2. read-only queries must survive: modules branch on them, and a stubbed
    # query silently changes which branch runs.
    for q in pkg_installed _pkgmap_one _spec_method service_resolve; do
        case " $OSR_APPLY_QUERY_OK " in
            *" $q "*) ;;
            *) fail "$q must stay callable in theme-only mode" ;;
        esac
    done
    _m=$(_pkgmap_one zsh 2>/dev/null || true)
    [ -n "$_m" ] && ok "_pkgmap_one still resolves after stubbing ($_m)" \
        || fail "_pkgmap_one was stubbed - modules would misbranch"
    rm -rf "$MARK"
    finish
) || exit 1

# --- 3. the layer set follows the installed rice ------------------------------
(
    . "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"; . "$OSR_LIB/theme.sh"; . "$OSR_LIB/apply.sh"

    _all=$(osr_theme_modules "" | wc -l | tr -d ' ')
    _i3=$(osr_theme_modules i3-rosemary | wc -l | tr -d ' ')
    _hypr=$(osr_theme_modules arch-hyprland-glass | wc -l | tr -d ' ')
    _cli=$(osr_theme_modules nord | wc -l | tr -d ' ')

    [ "$_i3" -lt "$_all" ] && ok "an i3 rice runs fewer layers ($_i3) than every module ($_all)" \
        || fail "rice narrowing did nothing (i3=$_i3 all=$_all)"
    [ "$_cli" -lt "$_i3" ] && ok "a shell-only rice runs fewer still ($_cli)" \
        || fail "expected the CLI rice to run the fewest layers (cli=$_cli i3=$_i3)"

    # The X11 rice must not paint Wayland bars and vice versa: that is the whole
    # point of narrowing by rice rather than globbing modules/.
    _i3_mods=$(osr_theme_modules i3-rosemary | tr '\n' ' ')
    case " $_i3_mods " in
        *" polybar "*) ok "the i3 rice includes polybar" ;;
        *) fail "the i3 rice should include polybar" ;;
    esac
    case " $_i3_mods " in
        *" waybar "*) fail "the i3 rice must not include waybar" ;;
        *) ok "the i3 rice excludes waybar" ;;
    esac
    _h_mods=$(osr_theme_modules arch-hyprland-glass | tr '\n' ' ')
    case " $_h_mods " in
        *" waybar "*) ok "the hypr rice includes waybar" ;;
        *) fail "the hypr rice should include waybar" ;;
    esac
    case " $_h_mods " in
        *" polybar "*) fail "the hypr rice must not include polybar" ;;
        *) ok "the hypr rice excludes polybar" ;;
    esac

    # Manifest directives must never be mistaken for module names.
    case " $_i3_mods " in
        *" theme "*|*" themes "*|*" require "*|*" rosemary "*)
            fail "a manifest directive leaked into the module list" ;;
        *) ok "theme:/themes:/require: lines are not treated as modules" ;;
    esac

    # Every name emitted must be a real module file - the list is fed straight
    # to `. modules/$name.sh`.
    _bad=""
    for m in $(osr_theme_modules ""); do
        [ -f "$OSR_ROOT/modules/$m.sh" ] || _bad="$_bad $m"
    done
    assert_eq "" "$_bad" "every emitted layer name is a real module"
    finish
) || exit 1

# --- 4/5/6. end-to-end: apply, re-apply, switch back --------------------------
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
HOME_DIR="$T/home"
mkdir -p "$HOME_DIR"

# osr_apply_theme is driven directly, NEVER `install.sh --theme-only`: install.sh
# resolves OSR_HOME from passwd, so running it here would apply the theme to the
# real home of whoever runs the suite. OSR_HOME is set after user.sh is sourced,
# which is the same seam every other unit test uses.
run_apply() {
    (
        OSR_HOME="$HOME_DIR"; OSR_USER=$(id -un); export OSR_HOME OSR_USER
        OSR_LOG="$T/apply.log"; export OSR_LOG
        NO_COLOR=1; export NO_COLOR
        . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/detect.sh"
        . "$OSR_LIB/user.sh"; . "$OSR_LIB/net.sh"; . "$OSR_LIB/pkg.sh"; . "$OSR_LIB/git.sh"
        . "$OSR_LIB/service.sh"; . "$OSR_LIB/config.sh"; . "$OSR_LIB/theme.sh"
        . "$OSR_LIB/state.sh"; . "$OSR_LIB/apply.sh"; . "$OSR_LIB/fonts.sh"
        . "$OSR_LIB/build.sh"
        osr_detect >/dev/null 2>&1
        # OSR_HOME must survive everything above: if a lib ever resolves it from
        # passwd again, this assert fires here instead of in someone's dotfiles.
        [ "$OSR_HOME" = "$HOME_DIR" ] || { echo "SANDBOX ESCAPE: OSR_HOME=$OSR_HOME"; exit 9; }
        osr_apply_theme "$1"
    ) 2>&1
}

# Pre-seed the state so the apply narrows to one small rice's modules
# (catppuccin: starship zsh fastfetch yazi) - the rice under test is the module
# SET, and the theme applied over it is deliberately a different name.
mkdir -p "$HOME_DIR/.config/osr"
printf 'rice=catppuccin\n' > "$HOME_DIR/.config/osr/state"

OUT=$(run_apply nord) || { printf '%s\n' "$OUT"; fail "theme apply exited non-zero"; }
printf '%s\n' "$OUT" | grep -q "applying theme 'nord'" \
    && ok "apply ran for nord" || { printf '%s\n' "$OUT"; fail "apply ran for nord"; }
printf '%s\n' "$OUT" | grep -q "SANDBOX ESCAPE" \
    && fail "the apply escaped the sandbox HOME" || ok "the apply stayed inside the sandbox HOME"

# The layers really landed.
[ -f "$HOME_DIR/.config/osr/zsh/rc.d/90-theme.zsh" ] \
    && ok "the zsh theme layer landed" || fail "the zsh theme layer landed"
assert_contains "$HOME_DIR/.config/osr/zsh/rc.d/90-theme.zsh" 'OSR_RICE_THEME="nord"' \
    "the zsh layer is nord's"
assert_contains "$HOME_DIR/.config/starship.toml" "#88c0d0" \
    "starship.toml was composed with nord's palette"

# ...and the state records it.
assert_contains "$HOME_DIR/.config/osr/state" "theme=nord" "state records the applied theme"
assert_contains "$HOME_DIR/.config/osr/state" "rice=catppuccin" "state keeps the rice"

# 5. switching to another theme replaces the layers (§6 replace semantics).
OUT=$(run_apply gruvbox) || { printf '%s\n' "$OUT"; fail "second apply exited non-zero"; }
assert_contains "$HOME_DIR/.config/osr/zsh/rc.d/90-theme.zsh" 'OSR_RICE_THEME="gruvbox"' \
    "the zsh layer is now gruvbox's"
assert_contains "$HOME_DIR/.config/starship.toml" "#fabd2f" \
    "starship.toml now carries gruvbox's palette"
refute_contains "$HOME_DIR/.config/starship.toml" "#88c0d0" \
    "no trace of the previous theme is left in the composed file"

# ...and back, byte-identically: a theme switch must be a pure function of the
# theme, not of the order themes were applied in.
run_apply nord >/dev/null
SUM1=$(cat "$HOME_DIR/.config/starship.toml")
run_apply gruvbox >/dev/null
run_apply nord >/dev/null
SUM2=$(cat "$HOME_DIR/.config/starship.toml")
assert_eq "$SUM1" "$SUM2" "A -> B -> A returns the identical file"

# Idempotent (§2): applying the same theme twice changes nothing.
BEFORE=$(find "$HOME_DIR/.config" -type f -newermt '@0' -printf '%p %s\n' 2>/dev/null | sort)
run_apply nord >/dev/null
AFTER=$(find "$HOME_DIR/.config" -type f -newermt '@0' -printf '%p %s\n' 2>/dev/null | sort)
assert_eq "$BEFORE" "$AFTER" "re-applying the same theme is a no-op"

# 4. no package manager was ever consulted. A stray call would have needed the
# network or sudo; assert on the run output rather than trusting that.
OUT=$(run_apply nord)
for forbidden in "Installing " "apt-get" "pacman" "xbps-install" "dnf install"; do
    case "$OUT" in
        *"$forbidden"*) fail "a theme apply ran a package step: $forbidden" ;;
        *) ;;
    esac
done
ok "no package/install step ran during a theme apply"

# 6. no sudo ticket -> root layers are skipped, not prompted for.
case "$OUT" in
    *"password"*|*"sudo:"*) fail "a theme apply prompted for sudo" ;;
    *) ok "no sudo prompt during a theme apply" ;;
esac

finish
