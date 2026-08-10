# session: x11+wayland
# modules/gpaste.sh — GPaste, the clipboard manager for GNOME sessions.
#
# GNOME-only by nature, not by preference: GPaste's daemon has no clipboard of
# its own to watch on Wayland. It reads the selection through the GNOME Shell
# extension, and the extension is also what grabs its hotkeys. That is why this
# module cares so much about the extension actually being enabled — a GPaste
# whose extension is off is not a degraded GPaste, it is an empty one.
#
# Both sessions, because GNOME runs both — but the modules that own the non-GNOME
# clipboard on each are cliphist.sh (Wayland/Hyprland) and copyq.sh (X11), and
# the GNOME-specific half below is gated so this module is inert next to them.
#
# The build lives in lib/build.sh (apt.map -> source:provide_gpaste): the distro
# package is version-mismatched against the Shell and has to be replaced, not
# configured around.
run_step "Installing GPaste" pkg_install gpaste

_gp_uuid="GPaste@gnome-shell-extensions.gnome.org"

_is_gnome=0
case "${XDG_CURRENT_DESKTOP:-}" in *GNOME*|*gnome*) _is_gnome=1 ;; esac
case "${XDG_SESSION_DESKTOP:-}" in *gnome*|*GNOME*) _is_gnome=1 ;; esac

if [ "$_is_gnome" -eq 1 ]; then
    # Enabling needs a live session bus, which an installer run over ssh or from
    # a TTY does not have. Not fatal: the settings below still land in dconf, and
    # the extension can be switched on from the Extensions app afterwards.
    _gpaste_enable_extension() {
        as_user gnome-extensions enable "$_gp_uuid" 2>/dev/null && return 0
        warn "could not enable the GPaste extension now - turn it on in the Extensions app (GPaste has no clipboard access without it)"
    }
    run_step "Enabling the GPaste shell extension" _gpaste_enable_extension

    # images-support is what makes a copied image enter the history at all, and
    # it is the setting that looks enabled-but-dead under a mismatched extension:
    # the daemon never sees the image to store. Setting it again after the
    # version fix is what actually makes it take effect.
    as_user gsettings set org.gnome.GPaste images-support true
    as_user gsettings set org.gnome.GPaste rich-text-support true

    # The daemon caches the history in memory; 30 MiB is upstream's default and
    # roughly two screenshots. Images are the reason to raise it.
    as_user gsettings set org.gnome.GPaste max-memory-usage 200

    # Pick up the new schema, D-Bus service and typelib: the running daemon is
    # still the old build until it is told otherwise.
    as_user systemctl --user daemon-reload 2>/dev/null || true
    as_user systemctl --user restart org.gnome.GPaste.service 2>/dev/null || true
fi
