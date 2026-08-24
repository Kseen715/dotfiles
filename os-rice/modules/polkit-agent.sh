# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/polkit-agent.sh — the polkit authentication agent (i3-sugg §3.1).
# Mandatory and silent when missing: with no agent running, every GUI action
# that needs root — mounting an internal disk, printer setup, virt-manager,
# blueman pairing, timeshift — fails with no dialog and no error.
#
# i3 starts no agent by itself; the i3 config execs the binary path below.
# polkit-gnome is the classic single-binary choice (mate-polkit and
# lxqt-policykit are drop-in alternatives, see i3-sugg §3.1).

run_step "Installing polkit agent" pkg_install polkit polkit-gnome

# The agent lives at a different path per distro; report the resolved one so a
# wrong `exec` line in the i3 config is obvious instead of mysterious.
for _pa in \
    /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 \
    /usr/libexec/polkit-gnome-authentication-agent-1 \
    /usr/lib/x86_64-linux-gnu/polkit-gnome/polkit-gnome-authentication-agent-1; do
    if [ -x "$_pa" ]; then
        info "polkit agent: $_pa"
        break
    fi
    _pa=""
done
[ -n "${_pa:-}" ] || warn "polkit agent binary not found - check the exec line in ~/.config/i3/config"
