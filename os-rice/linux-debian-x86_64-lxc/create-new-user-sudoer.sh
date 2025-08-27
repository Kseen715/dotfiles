#!/bin/bash

# Function to display help message
show_help() {
    echo "Usage: $0 <username>"
    echo ""
    echo "This script creates a new user with sudo privileges."
    echo "The password will be prompted securely (not visible when typing)."
    echo ""
    echo "Arguments:"
    echo "  <username>    The username for the new user"
    echo ""
    echo "Example:"
    echo "  $0 john"
    echo ""
    echo "Requirements:"
    echo "  - Must be run as root"
    echo "  - Username must not already exist"
}

# Check if correct number of arguments provided
if [ $# -ne 1 ]; then
    echo "Error: Incorrect number of arguments provided."
    echo ""
    show_help
    exit 1
fi

USERNAME="$1"

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run as root."
    echo "Please run with: sudo $0 $USERNAME"
    exit 1
fi

# Validate username (basic check)
if [[ ! "$USERNAME" =~ ^[a-zA-Z][a-zA-Z0-9_-]*$ ]]; then
    echo "Error: Invalid username format."
    echo "Username must start with a letter and contain only letters, numbers, hyphens, and underscores."
    echo ""
    show_help
    exit 1
fi

# Check if user already exists
if id "$USERNAME" &>/dev/null; then
    echo "Error: User '$USERNAME' already exists."
    echo ""
    show_help
    exit 1
fi

# Prompt for password securely
echo "Enter password for user '$USERNAME':"
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

# Create the user
echo "Creating user '$USERNAME'..."
if useradd -m -s /bin/bash "$USERNAME"; then
    echo "User '$USERNAME' created successfully."
else
    echo "Error: Failed to create user '$USERNAME'."
    exit 1
fi

# Set the password
echo "Setting password for user '$USERNAME'..."
if echo "$USERNAME:$PASSWORD" | chpasswd; then
    echo "Password set successfully."
else
    echo "Error: Failed to set password for user '$USERNAME'."
    # Clean up - remove the created user
    userdel -r "$USERNAME" 2>/dev/null
    exit 1
fi

# Add user to sudo group
echo "Adding user '$USERNAME' to sudo group..."
if usermod -aG sudo "$USERNAME"; then
    echo "User '$USERNAME' added to sudo group successfully."
else
    echo "Error: Failed to add user '$USERNAME' to sudo group."
    echo "User was created but does not have sudo privileges."
    exit 1
fi

# Clear password variables for security
unset PASSWORD
unset PASSWORD_CONFIRM

echo ""
echo "✓ User '$USERNAME' has been created successfully with sudo privileges!"
echo "The user can now log in with the provided password and use sudo commands."
