# test/ref/blueman_sh_ref.sh — the sh implementation of modules/blueman.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/blueman.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/blueman.sh — Bluetooth stack + tray applet (i3-sugg §7.2).
# blueman-applet is what the i3 config execs; without it there is no pairing UI
# and no way to answer a pairing request.
#
# The service name differs per init (bluetooth.service vs /etc/sv/bluetoothd) —
# that is a servicemap `@init` row, not a case here (§8).

# bluez-obex is file transfer to/from the phone; without it "Send file" in the
# blueman menu is greyed out. mpris-proxy (ships inside bluez) is what makes the
# play/pause button on a headset reach playerctl and the bar.
run_step "Installing Bluetooth" pkg_install bluez bluez-obex blueman

enable_service bluetooth || warn "could not enable bluetooth (needs a real init)"
