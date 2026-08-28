# test/ref/copyq_sh_ref.sh — the sh implementation of modules/copyq.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/copyq.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11
# themable: yes
# modules/copyq.sh — clipboard manager, the X11 replacement for cliphist
# (i3-sugg §2.1). Not optional on X11: a selection is owned by the process that
# made it, so closing the source app destroys what you copied. CopyQ owns the
# selection on everyone's behalf.
#
# Void spells it CopyQ (xbps.map carries the row); xsel covers the PRIMARY
# selection for scripts that expect it.

run_step "Installing CopyQ" pkg_install copyq xclip xsel

# CopyQ paints its own item list from a theme .ini, not from the Qt palette
# (theme-owned, §6b). Installed as a loadable preset under themes/ - CopyQ keeps
# the ACTIVE appearance inside copyq.conf, which is user territory here, so this
# is applied once from Preferences > Appearance > Load and then swaps with the
# theme on every later switch.
install_theme_layer copyq theme.ini "$OSR_HOME/.config/copyq/themes/osr.ini" || :

# ...and then actually APPLY it. Shipping the preset alone means every fresh
# install ends with the stock Qt blue-on-white list until someone finds
# Preferences > Appearance > Load — a themed desktop with one unthemed window in
# it, which is exactly the kind of gap §6 exists to close.
#
# CopyQ keeps the ACTIVE appearance in copyq.conf's [Theme] section (loading a
# preset just copies the keys in), so the apply is: drop the old [Theme] block,
# append ours. Everything outside that section is user territory - tabs,
# commands, the tray behaviour - and is carried through untouched.
_cq_theme="$OSR_HOME/.config/copyq/themes/osr.ini"
_cq_conf="$OSR_HOME/.config/copyq/copyq.conf"
if [ -f "$_cq_theme" ]; then
    as_user mkdir -p "$OSR_HOME/.config/copyq"
    [ -f "$_cq_conf" ] || as_user touch "$_cq_conf"
    run_step "Applying the CopyQ theme" as_user sh -c '
        _conf=$1
        _theme=$2
        _tmp=$_conf.osr-new
        # Everything except the existing [Theme] section. A section ends at the
        # next [Header] or at EOF.
        awk "BEGIN { keep = 1 } /^\[/ { keep = (\$0 != \"[Theme]\") } keep" "$_conf" >"$_tmp"
        # Our block, comments stripped - CopyQ rewrites this file itself and
        # would not preserve them anyway.
        printf "\n" >>"$_tmp"
        grep -v "^[[:space:]]*#" "$_theme" | grep -v "^[[:space:]]*$" >>"$_tmp"
        mv -f "$_tmp" "$_conf"
    ' sh "$_cq_conf" "$_cq_theme"

    # CopyQ reads copyq.conf at start and rewrites it at exit, so a running
    # instance would both ignore the new theme and clobber it on logout.
    if as_user pgrep -x copyq >/dev/null 2>&1; then
        as_user sh -c 'copyq exit >/dev/null 2>&1 || true'
        as_user sh -c 'copyq >/dev/null 2>&1 &' || :
    fi
fi
