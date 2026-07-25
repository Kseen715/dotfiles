# modules/printer.sh — CUPS + Samba/SMB client + Canon captdriver (AUR). POSIX
# port of .../modules/printer.sh. Services enabled via enable_service (§8). The
# empty /etc/samba/smb.conf is seeded so smbd starts. Real-hardware concern (§9).
run_step "Installing printing + SMB" pkg_install smbclient cups samba
run_step "Installing Canon captdriver (AUR)" pkg_install captdriver
if [ ! -f /etc/samba/smb.conf ]; then
    as_root mkdir -p /etc/samba
    as_root touch /etc/samba/smb.conf
fi
enable_service smb
enable_service cups
