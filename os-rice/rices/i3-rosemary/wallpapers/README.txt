Drop one image here (.jpg/.jpeg/.png/.webp) and it becomes this rice's wallpaper.

It is picked up automatically: osr_install_wallpaper copies it to
~/Pictures/Wallpapers, install_wallpaper_layer substitutes the installed path
into the i3 theme layer's feh line, modules/lightdm.sh points the greeter at it,
and modules/i3lock.sh primes betterlockscreen's blur cache from it.

This .txt is deliberately not an image, so a rice with no wallpaper resolves to
"nothing to set" rather than to this placeholder (see osr_rice_wallpaper).

Something dark, low-contrast and warm suits the palette — the rose accent
(#d98cae) is the only saturated color in the rice and it should stay that way.
