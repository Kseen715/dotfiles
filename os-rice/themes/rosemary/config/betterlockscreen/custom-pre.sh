# custom-pre.sh — theme-owned lock screen LAYOUT (i3-rosemary).
#
# betterlockscreen sources this from prelock(), after the rc and after it has
# reset `lockargs`, and it appends "${lockargs[@]}" LAST to the i3lock-color
# command line. That ordering is the whole mechanism: everything set here wins
# over betterlockscreen's built-in layout, which parks the clock and greeting in
# the bottom-left corner. There is no rc key for any of this - positions are not
# among the variables betterlockscreen reads.
#
# The layout is the glass rice's hyprlock screen ported to i3lock-color: one
# oversized clock on a blurred, dimmed shot of the desktop, the date under it,
# a greeting in the top-left, and a thin ring low on the screen instead of a
# panel. What is NOT copied is glass's colours - that file uses pure red/green/
# magenta as working placeholders. Everything below refers to the rc's palette
# variables instead, so this follows a theme switch like every other layer.
#
# Positions accept expressions over w/h (screen) and ix/iy, tx/ty (indicator,
# time). Align: 0 centre, 1 left, 2 right.

lockargs+=(
    # --- the clock: the whole point of the screen -------------------------
    --time-str="$time_format"
    --time-size=120
    --time-pos="w/2:h/2-40"
    --time-align=0
    --time-color="$timecolor"
    --time-font="$font"

    # Date under the clock. betterlockscreen passes an empty --date-str, so
    # without this line there is simply no date on the screen.
    --date-str="%A, %d %B"
    --date-size=22
    --date-pos="tx:ty+52"
    --date-align=0
    --date-color="$greetercolor"
    --date-font="$font"

    # --- greeting, top-left ----------------------------------------------
    --greeter-text="$locktext"
    --greeter-size=26
    --greeter-pos="60:70"
    --greeter-align=1
    --greeter-color="$greetercolor"
    --greeter-font="$font"

    # --- the ring: low, thin, out of the way ------------------------------
    # glass gives its input field no outline at all and lets the dots carry the
    # feedback. A ring is i3lock's only affordance, so it stays - but thin, and
    # far enough below the clock that the two do not read as one object.
    --radius=46
    --ring-width=4
    --ind-pos="w/2:h/2+190"

    # Status text beside the ring rather than betterlockscreen's default offsets,
    # which were computed for the bottom-left layout and land off-centre here.
    --verif-pos="ix:iy+74"
    --verif-align=0
    --verif-size=15
    --wrong-pos="ix:iy+74"
    --wrong-align=0
    --wrong-size=15
    --modif-pos="ix:iy+74"
    --modif-align=0
    --modif-size=15
    --layout-pos="ix:iy+96"
    --layout-align=0
    --layout-size=13
)
