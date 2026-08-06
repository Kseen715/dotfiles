# session: x11+wayland
# modules/keyring.sh — Secret Service on D-Bus (i3-sugg §3.5). VS Code, Chrome,
# the git credential helper, Nextcloud and Element all expect one; without it
# they either nag on every start or silently store nothing.
#
# gnome-keyring provides the daemon, libsecret the client API, gcr the prompt UI,
# seahorse the GUI. The PAM lines are what make the keyring unlock with your
# login password instead of asking again — they are appended to the DM's and the
# console's PAM stacks only if absent (idempotent, §2).

run_step "Installing keyring" pkg_install gnome-keyring libsecret gcr seahorse

# PAM wiring. `optional` on purpose: a broken keyring must never lock you out.
for _pam in /etc/pam.d/lightdm /etc/pam.d/login /etc/pam.d/sddm; do
    [ -f "$_pam" ] || continue
    if grep -q pam_gnome_keyring "$_pam" 2>/dev/null; then
        info "$_pam already wires pam_gnome_keyring - skipping"
        continue
    fi
    info "adding pam_gnome_keyring to $_pam"
    printf 'auth       optional  pam_gnome_keyring.so\nsession    optional  pam_gnome_keyring.so auto_start\n' \
        | as_root tee -a "$_pam" >/dev/null
done
