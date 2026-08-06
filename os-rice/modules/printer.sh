# session: x11+wayland
# modules/printer.sh — CUPS + Samba/SMB client + Canon captdriver (AUR). POSIX
# port of .../modules/printer.sh. Services enabled via enable_service (§8). The
# empty /etc/samba/smb.conf is seeded so smbd starts. Real-hardware concern (§9).
run_step "Installing printing + SMB" pkg_install smbclient cups samba

# The half people forget: cups alone prints to a queue you have no GUI to create.
# system-config-printer is that GUI; gutenprint/hplip are the driver sets for
# everything that is not driverless; cups-pdf gives a "Print to file" queue.
# Driverless network printers additionally need mDNS - modules/avahi.sh.
run_step "Installing printer drivers + GUI" pkg_install \
    cups-pdf system-config-printer gutenprint hplip

# Scanning: sane is the backend, sane-airscan adds driverless (eSCL/WSD) network
# scanners, simple-scan is the GUI.
run_step "Installing scanner support" pkg_install sane sane-airscan simple-scan
# captdriver is the Canon CAPT vendor driver and is packaged almost nowhere
# (AUR on Arch, absent on Void/Debian/Alpine). Everything except a CAPT-only
# Canon prints fine without it, so a missing package must degrade to a warning
# instead of aborting the whole rice (§9). The subshell contains error()'s exit.
if ! ( run_step "Installing Canon captdriver" pkg_install captdriver ); then
    warn "captdriver is not available on this distro - skipping (only Canon CAPT printers need it)"
fi
if [ ! -f /etc/samba/smb.conf ]; then
    as_root mkdir -p /etc/samba
    as_root touch /etc/samba/smb.conf
fi
enable_service smb
enable_service cups
