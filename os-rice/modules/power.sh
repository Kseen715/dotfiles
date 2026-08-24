# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/power.sh — battery, thermals and idle policy (i3-sugg §7.3).
#
# tlp is the pick; `power-profiles-daemon` and `auto-cpufreq` are the
# alternatives and running two of them means two things fighting over the same
# sysfs knobs, so this module *removes* power-profiles-daemon if it is present
# rather than installing on top of it (the same mirror-module pattern as
# pipewire/pulseaudio).
#
# batsignal is the battery alert: a bar module shows the percentage, but nothing
# warns you at 5% unless a daemon does. thermald is Intel-only and is skipped
# elsewhere — it does nothing on AMD and its absence is not a failure.
#
# Not packaged on Void (i3-void-packages.md): auto-cpufreq, optimus-manager,
# envycontrol, nvidia-prime. For hybrid graphics, switcheroo-control is the
# packaged option and is installed when two GPUs are detected.

if pkg_installed power-profiles-daemon 2>/dev/null; then
    warn "power-profiles-daemon is installed - removing it before tlp (never both)"
    disable_service power-profiles-daemon || :
    pkg_remove power-profiles-daemon || warn "could not remove power-profiles-daemon"
fi

run_step "Installing power management" pkg_install \
    tlp tlp-rdw upower acpid powertop cpupower batsignal

# thermald only ships Intel thermal tables; installing it on AMD is noise.
case "${OSR_CPU_MODEL:-}" in
    *Intel*|*intel*) run_step "Installing thermald (Intel)" pkg_install thermald
                     enable_service thermald || warn "could not enable thermald" ;;
    *)               info "non-Intel CPU - skipping thermald" ;;
esac

# Hybrid graphics: switcheroo-control is the D-Bus service GTK apps use for
# "Launch using Discrete Graphics Card".
if [ "${OSR_GPU_COUNT:-0}" -gt 1 ]; then
    run_step "Installing switcheroo-control (hybrid GPU)" pkg_install switcheroo-control
fi

enable_service tlp   || warn "could not enable tlp (needs a real init)"
enable_service acpid || warn "could not enable acpid (needs a real init)"
