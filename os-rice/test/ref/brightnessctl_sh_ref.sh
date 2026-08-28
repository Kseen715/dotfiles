# test/ref/brightnessctl_sh_ref.sh — the sh implementation of modules/brightnessctl.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/brightnessctl.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/brightnessctl.sh — backlight + sensors (i3-sugg §7.3/§7.4).
# brightnessctl is the one that still works on modern kernels (xbacklight talks
# to a randr property most drivers no longer expose); ddcutil does the same over
# DDC/CI for external monitors.
#
# lm_sensors feeds the bar's temperature module - run `sensors-detect` once
# after installing. Battery/thermal policy (upower, acpid, tlp, thermald) is
# modules/power.sh; this module is only about the two things you point at a
# specific device: its backlight and its sensors.

run_step "Installing backlight + sensors" pkg_install brightnessctl ddcutil lm_sensors
