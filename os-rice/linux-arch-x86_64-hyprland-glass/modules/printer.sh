info "Installing smbclient..."
trace pacman -S --needed --noconfirm smbclient cups samba
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm captdriver-git
trace mkdir -p /etc/samba
trace touch /etc/samba/smb.conf
trace chmod 644 /etc/samba/smb.conf
trace systemctl enable --now smb.service
trace systemctl enable --now cups.service
