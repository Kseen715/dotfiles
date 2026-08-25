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

    # No greeter. betterlockscreen always passes --greeter-text (it defaults to
    # the rice name), so the label has to be blanked here rather than simply not
    # set - the lock screen shows the clock and nothing else.
    --greeter-text=""

    # --- the indicator ----------------------------------------------------
    # i3lock-color has no text field: there is no box and there are no dots,
    # only this ring. glass gets away with a small, quiet input because hyprlock
    # fills it with dots as you type; a thin 46px ring that never changes
    # visibly reads as "there is nowhere to type" - which is exactly how it was
    # reported. So the ring is big enough for the per-keypress highlight
    # (--keyhl-color, set in the rc) to be unmissable, and thick enough that the
    # colour change on verify/fail is a change in a SHAPE rather than in a line.
    --radius=90
    --ring-width=10
    # +170, not +200: the three status lines hang off the bottom of the ring
    # (+124/+150/+174), so the lowest one lands at h/2+174+170 = 728 on a 768px
    # panel. At +200 it fell at 758 and the keyboard-layout line was clipped by
    # the screen edge.
    --ind-pos="w/2:h/2+170"

    # Status text, each on its own line. These are NOT mutually exclusive:
    # caps-lock (modif) shows at the same time as "wrong", and the keyboard
    # layout at the same time as either - so sharing one position stacks them on
    # top of each other, which is the overlap that was reported. verif and wrong
    # genuinely cannot both be on screen, so those two do share a line.
    --verif-pos="ix:iy+124"
    --verif-align=0
    --verif-size=15
    --wrong-pos="ix:iy+124"
    --wrong-align=0
    --wrong-size=15
    --modif-pos="ix:iy+150"
    --modif-align=0
    --modif-size=14
    --layout-pos="ix:iy+174"
    --layout-align=0
    --layout-size=13
)
