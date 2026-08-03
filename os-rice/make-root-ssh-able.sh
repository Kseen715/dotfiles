#!/bin/bash

# Function to display help message
show_help() {
    echo "Usage: $0"
    echo ""
    echo "This script enables root SSH login and sets the root password."
    echo "The password will be prompted securely (not visible when typing)."
    echo ""
    echo "What this script does:"
    echo "  - Sets a new password for the root user"
    echo "  - Enables PermitRootLogin in SSH configuration"
    echo "  - Restarts the SSH service to apply changes"
    echo ""
    echo "Requirements:"
    echo "  - Must be run as root"
    echo ""
    echo "Example:"
    echo "  $0"
}

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run as root."
    echo "Please run with: sudo $0"
    exit 1
fi

# Check for help flag
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    show_help
    exit 0
fi

echo "This script will enable root SSH login and set a new root password."
echo ""

# Prompt for password securely
echo "Enter new password for root user:"
read -s PASSWORD
echo ""

# Confirm password
echo "Confirm password:"
read -s PASSWORD_CONFIRM
echo ""

# Check if passwords match
if [ "$PASSWORD" != "$PASSWORD_CONFIRM" ]; then
    echo "Error: Passwords do not match."
    exit 1
fi

# Check if password is not empty
if [ -z "$PASSWORD" ]; then
    echo "Error: Password cannot be empty."
    exit 1
fi

echo "Applying changes..."

# Set root password and enable SSH root login
if echo "root:$PASSWORD" | chpasswd; then
    echo "✓ Root password updated successfully."
else
    echo "Error: Failed to set root password."
    exit 1
fi

# Enable PermitRootLogin in SSH config
if sed -i "/^#*PermitRootLogin/c\PermitRootLogin yes" /etc/ssh/sshd_config; then
    echo "✓ SSH configuration updated to allow root login."
else
    echo "Error: Failed to update SSH configuration."
    exit 1
fi

# Ensure PermitRootLogin line exists if it wasn't found
if ! grep -q "^PermitRootLogin" /etc/ssh/sshd_config; then
    echo "PermitRootLogin yes" >> /etc/ssh/sshd_config
    echo "✓ Added PermitRootLogin directive to SSH configuration."
fi

# Restart SSH service
if systemctl restart sshd; then
    echo "✓ SSH service restarted successfully."
else
    echo "Error: Failed to restart SSH service."
    echo "You may need to restart it manually: systemctl restart sshd"
    exit 1
fi

# Clear password variables for security
unset PASSWORD
unset PASSWORD_CONFIRM

echo ""
echo "✓ Root SSH access has been enabled successfully!"
echo "You can now SSH as root using the password you just set."
echo ""
echo "Security reminder: Consider using SSH keys instead of passwords for better security."
